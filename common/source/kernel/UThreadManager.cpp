#include "UThreadManager.h"
#include "Thread.h"
#include "assert.h"
#include "UserThread.h"
#include "ArchThreads.h"

UThreadManager* UThreadManager::instance_ = 0;

UThreadManager::UThreadManager()
{
}

UThreadManager *UThreadManager::instance() {
  if (unlikely(!instance_))
  {
    instance_ = new UThreadManager();
  }

  return instance_;
}

size_t UThreadManager::create_thread(pthread_t* thread,
                                     const pthread_attr_t* attr,
                                     void (*libc_exec)(void*(*start_routine)(void*), void*),
                                     void*(*start_routine)(void*),
                                     void* arg)
{
  assert(currentThread != nullptr && "The current thread is NULL. This should never happen");
  assert(currentThread->getType() != Thread::KERNEL_THREAD && "The current thread is a KERNEL THREAD");

  //Validate thread ptr, because we need to dereference
  if(thread == nullptr || (size_t)thread >= USER_BREAK)
  {
    return -1ULL;
  }

  UserThread* curr_thread = reinterpret_cast<UserThread*>(currentThread);

  assert(curr_thread->getParentProc() != nullptr && "Every user thread must have a parent user process");

  pthread_t ret_tid = 0;
  int ret_val = curr_thread->getParentProc()->createThread(&ret_tid, attr, libc_exec, start_routine, arg);

  *thread = ret_tid;
  return ret_val;
}

void UThreadManager::exit_thread(void *retval)
{
  assert(currentThread != nullptr && "The current thread is NULL. This should never happen");
  assert(currentThread->getType() != Thread::KERNEL_THREAD && "The current thread is a KERNEL THREAD");

  auto curr_thread = reinterpret_cast<UserThread*>(currentThread);
  assert(curr_thread->getParentProc() != nullptr && "Every user thread must have a parent user process");

  curr_thread->getParentProc()->exitThread(retval);
}

size_t UThreadManager::join_thread(pthread_t id, void** retval) {

  //Validate thread ptr, because we need to dereference
  if ((size_t) retval >= USER_BREAK) {
    return -1ULL;
  }

  debug(JOIN, "Request to join thread %ld\n", id);

  assert(currentThread != nullptr && "The current thread is NULL. This should never happen");
  assert(currentThread->getType() != Thread::KERNEL_THREAD && "The current thread is a KERNEL THREAD");

  UserThread *curr_thread = reinterpret_cast<UserThread *>(currentThread);

  assert(curr_thread->getParentProc() != nullptr && "Every user thread must have a parent user process");

  void* tmp_rv = nullptr;
  size_t res = curr_thread->getParentProc()->joinThread(curr_thread->getTID(), id, &tmp_rv);

  if(retval != nullptr)
  {
    *retval = tmp_rv;
  }

  return res;
}

size_t UThreadManager::cancel_thread(pthread_t id)
{
  debug(THREAD, "Request to cancel thread %ld\n", id);

  assert(currentThread != nullptr && "The current thread is NULL. This should never happen");
  assert(currentThread->getType() != Thread::KERNEL_THREAD && "The current thread is a KERNEL THREAD");

  auto* curr_thread = reinterpret_cast<UserThread *>(currentThread);

  assert(curr_thread->getParentProc() != nullptr && "Every user thread must have a parent user process");
  size_t res = curr_thread->getParentProc()->cancelThread(id);
  return res == 0 ? 0 : -1ULL;
}