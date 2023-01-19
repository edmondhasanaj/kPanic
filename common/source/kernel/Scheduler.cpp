#include "Scheduler.h"
#include "Thread.h"
#include "panic.h"
#include "ArchThreads.h"
#include "ArchCommon.h"
#include "kprintf.h"
#include "ArchInterrupts.h"
#include "KernelMemoryManager.h"
#include <ulist.h>
#include <UserThread.h>
#include "backtrace.h"
#include "ArchThreads.h"
#include "umap.h"
#include "ustring.h"
#include "Lock.h"

ArchThreadRegisters *currentThreadRegisters;
Thread *currentThread;

Scheduler *Scheduler::instance_ = 0;

Scheduler *Scheduler::instance()
{
  if (unlikely(!instance_))
    instance_ = new Scheduler();
  return instance_;
}

Scheduler::Scheduler()
{
  block_scheduling_ = 0;
  ticks_ = 0;
  clocks_per_interrupt_ = 0;
  old_clocks_per_interrupt_ = 0;
  prev_time_ = 0;
  calibrated_ = false;
  uthread_start_ = 0;
  uthread_end_ = 0;
  calibr_ticks_ = 0;


  addNewThread(&cleanup_thread_);
  addNewThread(&idle_thread_);
}

uint32 Scheduler::schedule()
{
  assert(!ArchInterrupts::testIFSet() && "Tried to schedule with Interrupts enabled");
  if (block_scheduling_ != 0)
  {
    debug(SCHEDULER, "schedule: currently blocked\n");
    return 0;
  }

  auto it = threads_.begin();
  for(; it != threads_.end(); ++it)
  {
    if((*it)->getType() == Thread::USER_THREAD)
    {
      UserThread* userThread = reinterpret_cast<UserThread*>((*it));
      if(userThread->getWakeUpTime() != 0 && userThread->getWakeUpTime() > getCurrentTime())
      {
        continue;
      } 
      if(/*userThread->getState() == Sleeping && */ userThread->getWakeUpTime() != 0 && userThread->getWakeUpTime() <= getCurrentTime())
      {
        userThread->resetWakeUp();
        //userThread->setState(Running);
      }
    }

    if((*it)->schedulable())
    {
      if(currentThread != NULL && currentThread->getType() == Thread::USER_THREAD)
      {
        auto* userThread = reinterpret_cast<UserThread*>(currentThread);
        uthread_end_ = uthread_start_ == 0 ? 0 : getCurrentTime();
        //debug(USERPROCESS, "ADD TO ACCUMULATOR of PID %ld caused by thread %s: %ld - %ld = %ld\n", userThread->getParentProc()->getPid(), userThread->getName(), uthread_end_, uthread_start_, uthread_end_ - uthread_start_);
        userThread->getParentProc()->incAccTime(uthread_end_ - uthread_start_); 
      }

      currentThread = *it;
      //Check if the user thread should be cancelled at this point
      if(currentThread->getType() == Thread::USER_THREAD)
      {
        uthread_start_ = getCurrentTime();
        auto* thread = static_cast<UserThread*>(currentThread);

        //We have to be in userspace to perform this, to not leak any resources
        if(thread->shouldCancel() && thread->switch_to_userspace_ == 1)
        {
          debug(SCHEDULER, "T[%ld] CANCELLATION OF THREAD happening in schedule()\n", thread->getTID());
          thread->setCancelled();
          thread->prepareCancellation();
        }
      }

      break;
    }
  }

  assert(it != threads_.end() && "No schedulable thread found");

  ustl::rotate(threads_.begin(), it + 1, threads_.end()); // no new/delete here - important because interrupts are disabled

  //debug(SCHEDULER, "Scheduler::schedule: new currentThread is %p %s, switch_to_userspace: %d\n", currentThread, currentThread->getName(), currentThread->switch_to_userspace_);

  uint32 ret = 1;

  if (currentThread->switch_to_userspace_)
  {
    currentThreadRegisters = currentThread->user_registers_;
  }
  else
  {
    currentThreadRegisters = currentThread->kernel_registers_;
    ret = 0;
  }
//  debug(SCHEDULER, "%s scheduled\n", currentThread->getName());
  return ret;
}

void Scheduler::addNewThread(Thread *thread)
{
  assert(thread);
  debug(SCHEDULER, "addNewThread: %p  %zd:%s\n", thread, thread->getTID(), thread->getName());
  if (currentThread)
    ArchThreads::debugCheckNewThread(thread);
  KernelMemoryManager::instance()->getKMMLock().acquire();
  lockScheduling();
  KernelMemoryManager::instance()->getKMMLock().release();
  threads_.push_back(thread);
  unlockScheduling();
}

void Scheduler::sleep()
{
  debug(SCHEDULER, "Putting thread with TID = %ld to sleep\n", currentThread->getTID());
  currentThread->setState(Sleeping);
  assert(block_scheduling_ == 0);
  yield();
}

void Scheduler::wake(Thread* thread_to_wake)
{
  // wait until the thread is sleeping
  while(thread_to_wake->getState() != Sleeping)
    yield();

  debug(SCHEDULER, "Waking thread with TID = %ld up\n", thread_to_wake->getTID());
  thread_to_wake->setState(Running);
}

void Scheduler::yield()
{
  assert(this);
  if (!ArchInterrupts::testIFSet())
  {
    assert(currentThread);
    kprintfd("Scheduler::yield: WARNING Interrupts disabled, do you really want to yield ? (currentThread %p %s)\n",
             currentThread, currentThread->name_.c_str());
    currentThread->printBacktrace();
  }
  ArchThreads::yield();
}

void Scheduler::cleanupDeadThreads()
{
  /* Before adding new functionality to this function, consider if that
     functionality could be implemented more cleanly in another place.
     (e.g. Thread/Process destructor) */
//  debug(SCHEDULER, "starting cleanup\n");
//  debug(SCHEDULER, "ToBeDestroyed: %d\n", ThreadState::ToBeDestroyed);
//  debug(SCHEDULER, "Running: %d\n", ThreadState::Running);
//  debug(SCHEDULER, "Sleeping: %d\n", ThreadState::Sleeping);
//  debug(SCHEDULER, "Terminated: %d\n", ThreadState::Terminated);

  lockScheduling();
  uint32 thread_count_max = threads_.size();
  if (thread_count_max > 1024)
    thread_count_max = 1024;
  Thread* destroy_list[thread_count_max];
  uint32 thread_count = 0;
  for (uint32 i = 0; i < threads_.size(); ++i)
  {
    Thread* tmp = threads_[i];
    if (tmp->getState() == ToBeDestroyed)
    {
      destroy_list[thread_count++] = tmp;
      threads_.erase(threads_.begin() + i); // Note: erase will not realloc!
      --i;
    }
    if (thread_count >= thread_count_max)
      break;
  }
  unlockScheduling();
  if (thread_count > 0)
  {
    for (uint32 i = 0; i < thread_count; ++i)
    {
//      if(destroy_list[i]->getType() == Thread::USER_THREAD &&
//        ((UserThread*)destroy_list[i])->getP)
      delete destroy_list[i];
    }
    debug(SCHEDULER, "cleanupDeadThreads: done\n");
  }
}

void Scheduler::printThreadList()
{
  lockScheduling();
  debug(SCHEDULER, "Scheduler::printThreadList: %zd Threads in List\n", threads_.size());
  for (size_t c = 0; c < threads_.size(); ++c)
    debug(SCHEDULER, "Scheduler::printThreadList: threads_[%zd]: %p  %zd:%s     [%s]\n", c, threads_[c],
          threads_[c]->getTID(), threads_[c]->getName(), Thread::threadStatePrintable[threads_[c]->state_]);
  unlockScheduling();
}

void Scheduler::lockScheduling() //not as severe as stopping Interrupts
{
  if (unlikely(ArchThreads::testSetLock(block_scheduling_, 1)))
    kpanict("FATAL ERROR: Scheduler::*: block_scheduling_ was set !! How the Hell did the program flow get here then ?\n");
}

void Scheduler::unlockScheduling()
{
  block_scheduling_ = 0;
}

bool Scheduler::isSchedulingEnabled()
{
  if (this)
    return (block_scheduling_ == 0);
  else
    return false;
}

bool Scheduler::isCurrentlyCleaningUp()
{
  return currentThread == &cleanup_thread_;
}

uint32 Scheduler::getTicks()
{
  return ticks_;
}

void Scheduler::incTicks()
{
  if(ticks_ % CALIBRATION_FREQUENCY == 0)
  {
    calibrated_ = false;
    calibr_ticks_ = 0;
    prev_time_ = 0;
    old_clocks_per_interrupt_ = clocks_per_interrupt_;
    clocks_per_interrupt_ = 0;
    //debug(SCHEDULER, "Scheduler::::::::::::::::::::::::::::::::TICKS %ld\n",ticks_);
  }
  if(calibr_ticks_ < CALIBRATION_INTERRUPTS && !calibrated_)
  {
    size_t time = getCurrentTime();
    clocks_per_interrupt_ += prev_time_ == 0 ? 0 : (time - prev_time_);
    prev_time_ = time;
    calibr_ticks_++;
  }
  if(calibr_ticks_ == CALIBRATION_INTERRUPTS && !calibrated_)
  {
    clocks_per_interrupt_ /= (CALIBRATION_INTERRUPTS - 1);
    calibrated_ = true;
    //debug(SCHEDULER, "Scheduler::::::::::::::::::::::::::::AVG_TICKS %ld | %ld\n",ticks_, clocks_per_interrupt_);
  } 

  ++ticks_;
}

void Scheduler::printStackTraces()
{
  lockScheduling();
  debug(BACKTRACE, "printing the backtraces of <%zd> threads:\n", threads_.size());

  for (ustl::list<Thread*>::iterator it = threads_.begin(); it != threads_.end(); ++it)
  {
    (*it)->printBacktrace();
    debug(BACKTRACE, "\n");
    debug(BACKTRACE, "\n");
  }

  unlockScheduling();
}

void Scheduler::printLockingInformation()
{
  size_t thread_count;
  Thread* thread;
  lockScheduling();
  kprintfd("\n");
  debug(LOCK, "Scheduler::printLockingInformation:\n");
  for (thread_count = 0; thread_count < threads_.size(); ++thread_count)
  {
    thread = threads_[thread_count];
    if(thread->holding_lock_list_ != 0)
    {
      Lock::printHoldingList(threads_[thread_count]);
    }
  }
  for (thread_count = 0; thread_count < threads_.size(); ++thread_count)
  {
    thread = threads_[thread_count];
    if(thread->lock_waiting_on_ != 0)
    {
      debug(LOCK, "Thread %s (%p) is waiting on lock: %s (%p).\n", thread->getName(), thread,
            thread->lock_waiting_on_ ->getName(), thread->lock_waiting_on_ );
    }
  }
  debug(LOCK, "Scheduler::printLockingInformation finished\n");
  unlockScheduling();
}

size_t Scheduler::getCurrentTime()
{
  size_t temp0 = 0;
  asm volatile ("rdtsc;"
              "shl  $32, %%rdx;"
              "or %%rdx, %%rax"
              : "=a" (temp0)
              :
              : "rdx","rcx");
  return temp0;
}

size_t Scheduler::getCurrentAVGClocksPerInterrupt()
{
  if(calibrated_)
  {
    return clocks_per_interrupt_;
  } 
  else if(!calibrated_ && old_clocks_per_interrupt_ == 0) //first ticks
  {
    return clocks_per_interrupt_/ticks_;
  }
  else
  {
    return old_clocks_per_interrupt_;
  }
}

bool Scheduler::getCalibrated()
{
  return calibrated_;
}

size_t Scheduler::getUThreadStartTime()
{
  return uthread_start_;
}

