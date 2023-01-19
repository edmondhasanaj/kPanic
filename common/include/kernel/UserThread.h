#pragma once

#include "Thread.h"
#include "UserProcess.h"

class UserProcess;
class Thread;

class UserThread : public Thread
{
friend void internalThreadCancel();

public:
  UserThread(FileSystemInfo* working_dir, ustl::string name, Thread::TYPE type, UserProcess* parent_proc,
             size_t tid, void* entry_point);

  UserThread(UserThread* thread, UserProcess* parent_proc, size_t tid);

  ~UserThread();

  virtual void Run() override;

  UserProcess* getParentProc() const;

  /**
   * Stops the execution of thread at this point and
   * writes the val as return value. This thread will now be joinable
   * for other threads. Once it is also joined, then this thread will eventually
   * be removed. ALWAYS CALL THIS FROM A USERPROCESS. NEVER CALL IT DIRECTLY
   * @param val
   */
  void exit(void* val);

  uint64 getUserStackStartAddr();

  /**
   * @return Returns number of used pages of the user stack
   */
  uint16 getStackPages();

  /**
   * Indicates how much pages the threads user stack uses.
   * At time of creation the stack will only get 1 page assigned.
   * If stack size increases the number of used pages may increase.
   * @param stack_pages number of currently used stack pages
   */
  void setStackPages(uint16 stack_pages);

  /**
   * delete some resources, so that CleanupThread has less work
   */
  void deleteResources();

  /**
   * True if the thread is not yet finished
   * @return
   */
  bool isCancelableState();

  /**
   * Receives a cancel request
   */
  void receiveCancelRequest();

  /**
   * Returns true if this thread received a cancel request already, and
   * it wasn't canceled. (see setCancelled)
   * @return
   */
  bool shouldCancel();

  /**
   * When an actual cancellation begins, this should be called to mark this
   * as not cancellable anymore.
   */
  void setCancelled();

  /**
   * If this is called, the registers will be cleared, so that this thread
   * can execute the internal cancellation process. Should only be
   * called on a successful 'shouldCancel()' call.
   */
  void prepareCancellation();

  /**
   * Sets wakeup time of this thread that will be later checked in scheduler.
   * @param wakeup rdtsc increment until which this thread will sleep
   */
  void sleepUntil(uint64_t wakeup);

  /**
   * Resets wakeup time of this thread back to zero, should happen before
   * this thread is set back to schedulable.
   */
  void resetWakeUp();

  uint64_t getWakeUpTime();

  int waitForAnyPid();
  int waitForPid(size_t pid);
  /*
   * Creates new stack
   *         new registers
   *         sets cr3 to new pml4
   */
  void restructureUserThread(Loader* new_loader, ustl::string new_name);

  /**
   * True if this thread was exited already.
   * @return
   */
  bool wasReallyExited();

  /**
   * Sets the exited to true. After this, it can't go back to false
   */
  void setReallyExited();

private:
  UserProcess* parent_proc_;

  uint64 stack_start_addr_;
  uint64 stack_end_addr_;
  uint16 stack_pages_;

  ustl::atomic<bool> cancel_req_;

  uint64_t time_to_wakeup_;
  ustl::atomic<bool> cancelable_;
  ustl::atomic<bool> exited_;
};