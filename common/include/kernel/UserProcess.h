#pragma once

#include <umap.h>
#include "Loader.h"
#include "UserThread.h"
#include "Mutex.h"
#include "Condition.h"
#include "RingBuffer.h"

#define PIPE_BUF_SIZE 1024
#define PIPE_FD_CODE -2
#define PIPE_FD_CLOSED -3

struct UserStackInfo
{
    pointer start_address_; // high address
    pointer end_address_;   // low  address -> this address is excluded from the stack
    //uint16* pages_in_use_;  // number of pages in use
};

typedef size_t pthread_t;
typedef unsigned int pthread_attr_t;

typedef struct WaitingInfo {
  pthread_t waiter_;
  Condition* cond_;
} WaitingInfo;

class UserThread;

class UserProcess
{
public:
  /**
   * Constructor
   * @param minixfs_filename filename of the file in minixfs to execute
   * @param fs_info filesysteminfo-object to be used
   * @param terminal_number the terminal to run in (default 0)
   *
   */
  UserProcess(size_t pid, ustl::string minixfs_filename, FileSystemInfo *fs_info, uint32 terminal_number = 0);

  UserProcess(const UserProcess& proc);

  virtual ~UserProcess();

  ///------------------------------- GETTERS & SETTERS BEGIN -------------------------------

  size_t getPid() const;

  Loader* getLoader() const;

  ustl::string getFilename() const;

  FileSystemInfo* getFsInfo() const;

  uint32 getTerminalNumber() const;

  Terminal* getTerminal() const;

  void setTerminal(Terminal* my_term);

  //uint32 getTerminalNumber() const;

  uint64_t getAccTime() const;

  ustl::map<int, int> getLocalFDs() const;

  size_t getOrigLocalFD() const;

  size_t getFdNum() const;


  ///-------------------------------- GETTERS & SETTERS END --------------------------------

  /**
   * returns a pointer to the first thread (order not guaranteed) of the user process.
   * Could be used in the beginning to get the first thread the process creates.
   *
   * @return a pointer to the first thread or nullptr if there is no initial thread
   */
  UserThread* getFirstThread();

  /**
   * Creates a thread that shares its resources with us, with the given name,
   * and the given entry point of the thread. Thread safe. Returns the ptr to the thread
   * if successful, or NULL if OUT-OF-MEM
   * @param name
   * @param entry_point
   */
  UserThread* addNewThread(ustl::string name, void* entry_point, bool on_fork);

  /**
   * Called by threads of this process when they get completely destroyed
   * @param id
   */
  void onThreadDestroyed(size_t id);

  /**
   * ASLR … Address Space Layout Randomization.
   * Creates new stacks with an random address within the StackSpace.
   * StackSpace = upper two thirds of UserSpace.
   * @param tid … thread ID
   * @return start address of the new stack
   */
  uint64 ASLRStackManager(size_t tid);

  /**
   * adds a UserThread' stack_start and stack_end to user_stack_list_
   * (this function gets called when fork())
   * @param tid the tid of the calling UserThread
   * @param stack_start the stack start address
   * @param stack_end the stack end address
   */
  void addUserStackInformation(size_t tid, uint64 stack_start, uint64 stack_end);

  /**
   * Checks if tid-key is present in user_stack_list_ and erases map entries.
   * @param key the tid_
   */
  void eraseStackListEntry(size_t tid);

  /**
   * Returns start address of the arguments page.
   * @return args_seg_addr_
   */
  uint64 getArgsStartAddr();

  /**
   * not sure if this is an elegant way to do this
   * calls kill() on all threads in thread_list_ and deletes the resources of the process
   * that are already safe to delete
   */
  void kill();

  /**
   * delete resources with checks
   * @param delete_loader handles whether loader_ gets deleted or not
   */
  void deleteResources(bool delete_loader);

  /**
   * increases the rdtsc increments accumulator variable by the given value
   * @param time time a thread of this process was scheduled
   */
  void incAccTime(size_t time);

  /**
   * adding local fd that will point to global fd
   * @param global_fd global fd to point to
   * @return local fd 
   */
  size_t addLocalFD(int global_fd);

  /**
   * removing local fd 
   * @param global_fd global fd that identifies local fd
   */
  void removeLocalFD(int global_fd);

  /**
   * getting global fd from local fd 
   * @param local_fd local fd to resolve
   */
  size_t getGlobalFD(int local_fd);

  size_t openPipe(size_t read, size_t write);
  size_t readFromPipe(size_t read_fd, pointer buffer, size_t count);
  size_t writeToPipe(size_t write_fd, pointer buffer, size_t count);
  bool isPipe(int local_fd);
  void closePipe(int local_fd);
  ustl::map<ustl::pair<int, int>, RingBuffer<char>*> getPipes() const;

  /**
   * Creates a new thread and adds it to this process
   * @param thread
   * @param attr
   * @param libc_exec
   * @param start_routine
   * @param arg
   */
  size_t createThread(pthread_t* thread, const pthread_attr_t* attr, void (*libc_exec)(void*(*start_routine)(void*), void*),
                    void*(*start_routine)(void*),
                    void* arg);

  /**
   * Exits the current thread with the given ret val.
   * @param tid
   * @param retval
   */
  void exitThread(void* retval);

  /**
   * Joins the given thread and receives its ret val.
   * @param caller
   * @param tid
   * @param retval
   */
  size_t joinThread(pthread_t caller, pthread_t tid, void** retval);

  /**
   * Cancels the given thread
   * @param id
   * @return
   */
  size_t cancelThread(pthread_t id);

  /**
   * Check if the given address is within any of the user stacks.
   * @param address to check.
   * @return NULL if address is not within any user stack,
   *         otherwise reference of the according thread.
   */
  UserThread* addrIsWithinAnyUserStack(size_t address);

  /**
   * Kills all threads within the process, but the given one (tid).
   * @param tid: Only thread that survives.
   */
  void killThreadsSiblings(size_t tid);

  /**
   * Restructures current process as well as the current thread for execv.
   * @param filename String containing path and filename (/usr/filename)
   * @param args arguments list
   * @param argc number of arguments in list
   */
  size_t restructureProcessForExecv(ustl::string filename, pointer args, int argc);

private:
  //NOTE: All the private members are NOT THREAD SAFE
  size_t allocNextFreeTID();

  /**
   * Reads time-stamp-counter, i.e. number of cycles since reboot.
   * @return cycles since reboot.
   */
  uint64_t readTSC();

  /**
   * Creates a pseudo random number using the TSC as seed.
   * @return A pseudo random number.
   */
  uint64_t mersenneTwister();

  /**
   * checks if the key is present in thread_list_ and user_stack_list_ and
   * erases the map entries
   * @param key the tid_
   */
  void eraseThreadInformation(size_t tid);

  /**
   * takes a key checks if that key is present in thread_list_
   * @param key the tid_
   * @return true -> the key is present / false -> the key isn't present
   */
  bool isThreadTidInThreadList(size_t tid);

  /**
   * takes a key checks if that key is present in user_stack_list_
   * @param key the tid_
   * @return true -> the key is present / false -> the key isn't present
   */
  bool isThreadTidInUserStackList(size_t tid);

  /**
   * calls the .empty() on thread_list_
   * @return true -> the map is empty / false -> the map isn't empty
   */
  bool checkIfThreadListIsEmpty();

private:
  /**
   * Adds a new thread
   * @param name
   * @param entry_point
   * @param on_fork
   */
  UserThread* internalAddNewThread(ustl::string name, void *entry_point, bool on_fork);

  /**
   * If this process has a thread with the given id, then this
   * will return it, otherwise it will return false
   * @param tid
   * @param ret
   * @return
   */
  bool getThread(size_t tid, UserThread** ret);

  void acquireThreadsListLock();

  void releaseThreadsListLock();

  void acquireWaitersLock();

  void releaseWaitersLock();

  bool joinDeadlock(pthread_t caller, pthread_t target);

  size_t pid_;

  int32 fd_;
  ustl::string filename_;
  Loader* loader_;
  FileSystemInfo* fs_info_;

  uint32 terminal_number_;
  Terminal* my_terminal_;

  size_t next_tid_;
  ustl::map<size_t, UserThread*> thread_list_;
  ustl::map<size_t, UserStackInfo> user_stack_list_;

  Mutex thread_list_lock_;
  Mutex user_stack_list_lock_;

  uint64 args_seg_addr_;
  size_t args_ppn_;

  ustl::map<pthread_t, WaitingInfo> waiting_list_;
  ustl::map<pthread_t, void*> ret_values_;
  Mutex waiters_lock_;

  /**
   * only one thread should be able to call UserProcess::kill(), all others call
   * UserThread::exit() after calling UserProcess::kill()
   */
  ustl::atomic<bool> called_exit_;

  uint64_t accumulated_incs_;
  ustl::map<int, int> fds_;  // process local fd -> global fd
  ustl::map<ustl::pair<int, int>, RingBuffer<char>*> pipes_;  // process <local fd -> global fd> -> ringbuf
  size_t fd_num_;
  Mutex fds_lock_;
  Mutex pipes_lock_;

  ustl::vector<pthread_t> tid_list_;
};

