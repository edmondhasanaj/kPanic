#include <ProcessRegistry.h>
#include "UserThread.h"
#include "ArchThreads.h"
#include "PageManager.h"
#include "offsets.h"
#include "assert.h"
#include "Scheduler.h"
#include "ArchInterrupts.h"
#include "UThreadManager.h"

UserThread::UserThread(FileSystemInfo* working_dir, ustl::string name, Thread::TYPE type,
                       UserProcess* parent_proc, size_t tid, void* entry_point)
  : Thread(new FileSystemInfo(*working_dir), name, type), parent_proc_(parent_proc),
           cancel_req_(false), time_to_wakeup_(0), cancelable_(true), exited_(false)
{
  debug(THREAD, "UserThread '%s' is being created\n", this->getName());
  tid_ = tid;
  t_loader_ = parent_proc_->getLoader();

  stack_start_addr_ = parent_proc_->ASLRStackManager(tid_);
  stack_end_addr_ = stack_start_addr_ - STACK_MAX_SIZE;
  setStackPages(1);

  size_t vpn = stack_start_addr_ / PAGE_SIZE - 1;
  size_t ppn = PageManager::instance()->allocPPN();
  bool mapped = t_loader_->arch_memory_.mapPage(vpn, ppn, 1);

  assert(mapped && "Virtual page for stack was already mapped - this should never happen");

  ArchThreads::createUserRegisters(user_registers_,
                                   entry_point,
                                   (void*) (stack_start_addr_),
                                   getKernelStackStartPointer());

  ArchThreads::setAddressSpace(this, t_loader_->arch_memory_);  // cr3 = pml4

  if(entry_point == 0)
    debug(THREAD, "The entry_point is NULL!\n");

  switch_to_userspace_ = 1;
  debug(THREAD, "UserThread '%s' has been created\n", this->getName());
}

UserThread::UserThread(UserThread* thread, UserProcess* parent_proc, size_t tid)
  : Thread(new FileSystemInfo(*(thread->working_dir_)),
           parent_proc->getFilename() + " - pid: " + ustl::to_string(parent_proc->getPid()) + " - tid: " + ustl::to_string(tid), thread->getType()),
           parent_proc_(parent_proc)
{
  debug(THREAD, "UserThread '%s' is being created\n", this->getName());

  assert(working_dir_ != nullptr && "working direcroty can't be NULL");

  tid_ = tid;

  t_loader_ = parent_proc_->getLoader();

  time_to_wakeup_ = 0;

  stack_start_addr_ = thread->stack_start_addr_;
  stack_end_addr_   = thread->stack_end_addr_;

  parent_proc_->addUserStackInformation(tid_, stack_start_addr_, stack_end_addr_);

  user_registers_ = new ArchThreadRegisters{*thread->user_registers_};

  user_registers_->rsp0 = kernel_registers_->rsp;

  ArchThreads::setAddressSpace(this, t_loader_->arch_memory_);

  switch_to_userspace_ = 1;

  debug(THREAD, "UserThread '%s' has been created\n", this->getName());
}


UserThread::~UserThread()
{
  //debug(THREAD, "~UserThread: freeing ThreadInfos\n");
  debug(THREAD, "~UserThread: tid: %lu\n", this->getTID());
  getParentProc()->onThreadDestroyed(this->getTID());
//  deleteResources();
  debug(THREAD, "~UserThread: done (%s)\n", name_.c_str());
}

void UserThread::Run()
{
}

UserProcess* UserThread::getParentProc() const
{
  return parent_proc_;
}

void UserThread::exit(void* val)
{
  assert(getState() != ToBeDestroyed && "A thread can't exit twice");
  debug(THREAD, "Thread %s with TID %ld is exiting\n", getName(), getTID());
  debug(THREAD, "thread EXIT with Ret val: %p\n", val);

  deleteResources();

  setState(ToBeDestroyed);
}

uint64 UserThread::getUserStackStartAddr()
{
  return stack_start_addr_;
}

uint16 UserThread::getStackPages()
{
  return stack_pages_;
}

void UserThread::setStackPages(uint16 stack_pages)
{
  stack_pages_ = stack_pages;
}

void UserThread::deleteResources() {

  if(user_registers_ != nullptr) {
    delete user_registers_;
    user_registers_ = nullptr;
  }

  if(working_dir_ != nullptr) {
    delete working_dir_;
    working_dir_ = nullptr;
  }
}

bool UserThread::isCancelableState()
{
  return getState() == Sleeping || getState() == Running;
}

void UserThread::receiveCancelRequest()
{
  cancel_req_ = true;
}

bool UserThread::shouldCancel()
{
  return cancelable_ && cancel_req_;
}

void UserThread::setCancelled()
{
  cancelable_ = false;
}

void internalThreadCancel()
{
  debug(THREAD, "Internal Thread cancellation for tid: %ld\n", currentThread->getTID());
  static_cast<UserThread*>(currentThread)->getParentProc()->exitThread((void*) -1);
}

void UserThread::prepareCancellation()
{
  debug(THREAD, "Preparing thread with tid for cancellation: %ld\n", getTID());
  kernel_registers_->rip = (size_t) internalThreadCancel;
  switch_to_userspace_ = 0;
}

bool UserThread::wasReallyExited()
{
  return exited_;
}

void UserThread::setReallyExited()
{
  exited_ = true;
}
void UserThread::restructureUserThread(Loader* new_loader, ustl::string new_name)
{
  debug(THREAD, "Restructuring UserThread '%s' ...\n", this->getName());
  name_ = new_name;

  t_loader_ = new_loader;

  stack_start_addr_ = parent_proc_->ASLRStackManager(tid_);
  stack_end_addr_ = stack_start_addr_ - STACK_MAX_SIZE;
  setStackPages(1);

  size_t vpn = stack_start_addr_ / PAGE_SIZE - 1;
  size_t ppn = PageManager::instance()->allocPPN();

  bool mapped = new_loader->arch_memory_.mapPage(vpn, ppn, 1);
  assert(mapped && "Virtual page for stack was already mapped - this should never happen");

  ArchThreads::createUserRegisters(user_registers_,
                                   new_loader->getEntryFunction(),
                                   (void*) (stack_start_addr_),
                                   getKernelStackStartPointer());

  ArchThreads::setAddressSpace(this, new_loader->arch_memory_); // cr3 = pml4
  debug(THREAD, "Restructured UserThread '%s'!\n", this->getName());
}

void UserThread::sleepUntil(uint64_t wakeup)
{
  if(wakeup < Scheduler::instance()->getCurrentTime())
  {
    return;
  }
  time_to_wakeup_ = wakeup;
  //debug(THREAD, "PID %ld |TID %ld| SLEEP UNTIL: %ld | STATE: %d\n", parent_proc_->getPid(), getTID(), getWakeUpTime(), getState()); 
  //setState(Sleeping);
  Scheduler::instance()->yield();
}

uint64_t UserThread::getWakeUpTime()
{
  //debug(THREAD, "SLEEP UNTIL: %ld\n", time_to_wakeup_);
  return time_to_wakeup_;
}

void UserThread::resetWakeUp()
{
  time_to_wakeup_ = 0;
}

int UserThread::waitForPid(size_t pid)
{
  auto pr = ProcessRegistry::instance();
  Condition* waitpid_cond = pr->getCondToWait(pid);
  if(waitpid_cond == NULL)
  {
    return -1;   // NO SUCH PID FOUND | OR DEADLOCK
  }
  pr->waitpid_map_lock_.acquire();
  waitpid_cond->wait(false);
  return (int)pid;
}

int UserThread::waitForAnyPid()
{
  size_t last_pid = 0;
  auto pr = ProcessRegistry::instance();
  pr->waitpid_map_lock_.acquire();
  pr->any_process_died_.wait();
  last_pid = pr->getLastDeadPID();
  pr->waitpid_map_lock_.release();
  return (int)last_pid;
}
