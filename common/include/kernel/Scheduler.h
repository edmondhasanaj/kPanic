#pragma once

#include "types.h"
#include <ulist.h>
#include "IdleThread.h"
#include "CleanupThread.h"
#include "Mutex.h"
#include "Condition.h"


#define CALIBRATION_INTERRUPTS 51 // +1 to account for the first interrupt
#define CALIBRATION_FREQUENCY 300 // Every 300 ticks recalibrate

class Thread;
class Mutex;
class SpinLock;
class Lock;

class Scheduler
{
  public:
    static Scheduler *instance();

    void addNewThread(Thread *thread);
    void sleep();
    void wake(Thread *thread_to_wake);
    void yield();
    void printThreadList();
    void printStackTraces();
    void printLockingInformation();
    bool isSchedulingEnabled();
    bool isCurrentlyCleaningUp();
    void incTicks();
    uint32 getTicks();

    size_t getCurrentTime();
    size_t getCurrentAVGClocksPerInterrupt();
    bool getCalibrated();
    size_t getUThreadStartTime();

    /**
     * NEVER EVER EVER CALL THIS METHOD OUTSIDE OF AN INTERRUPT CONTEXT
     * this is the method that decides which threads will be scheduled next
     * it is called by either the timer interrupt handler or the yield interrupt handler
     * and changes the global variables currentThread and currentThreadRegisters
     * @return 1 if the InterruptHandler should switch to Usercontext or 0 if we can stay in Kernelcontext
     */
    uint32 schedule();

  protected:
    friend class IdleThread;
    friend class CleanupThread;

    void cleanupDeadThreads();

  private:
    Scheduler();

    /**
     * Scheduler internal lock abstraction method
     * locks the thread-list against concurrent access by prohibiting a thread switch
     * don't call this from an Interrupt-Handler, since Atomicity won't be guaranteed
     */
    void lockScheduling();

    /**
     * Scheduler internal lock abstraction method
     * unlocks the thread-list
     */
    void unlockScheduling();

    static Scheduler *instance_;

    typedef ustl::list<Thread*> ThreadList;
    ThreadList threads_;

    size_t block_scheduling_;

    size_t ticks_;

    size_t clocks_per_interrupt_;
    size_t old_clocks_per_interrupt_;

    size_t prev_time_;

    size_t uthread_start_;
    size_t uthread_end_;
    
    IdleThread idle_thread_;
    CleanupThread cleanup_thread_;
    size_t calibr_ticks_;

    bool calibrated_;

    
};
