#pragma once

#include "umap.h"
#include "upair.h"
#include "Thread.h"
#include "Mutex.h"
#include "Condition.h"

class ProcessRegistry : public Thread
{
  public:
    /**
     * Constructor
     * @param root_fs_info the FileSystemInfo
     * @param progs a string-array of the userprograms which should be executed
     */
    ProcessRegistry ( FileSystemInfo *root_fs_info, char const *progs[] );
    ~ProcessRegistry();

    /**
     * Mounts the Minix-Partition with user-programs and creates processes
     */
    virtual void Run();

    /**
     * Tells us that a userprocess is being destroyed and clears the calling process's
     * pid from pid_list_
     * @param pid the pid to erase from the list
     */
    void processExit(size_t pid);

    /**
     * Tells us that a userprocess is being created due to a fork or something similar
     * @return the pid for the calling process
     */
    void processStart(UserProcess* proc);

    /**
     * Tells us how many processes are running
     */
    size_t processCount();

    static ProcessRegistry* instance();

    /**
     * create a new process and add it to process_list_
     * @param path
     */
    void createProcess(const char* path);
    UserProcess* createProcessOnFork();

   /**
    * returns a pointer to a UserProcess specified by it's pid
    * @param pid the desired pid
    * @return pointer to UserProcess on success | nullptr on failure
    */
   UserProcess* getUserProcessFromPid(size_t pid);

    /**
     * returns a new pid and increments pid_counter_
     */
    size_t getNewPID();

    void insertIntoProcessList(UserProcess* proc);


     void addToWaitPIDMap(size_t pid);
     void removeFromWaitPIDMap(size_t pid);
     Condition* getCondToWait(size_t pid);
     size_t getLastDeadPID();
     

     Mutex waitpid_map_lock_;
     Condition any_process_died_;
  private:
    void eraseFromProcessList(size_t pid);
    bool pidInProcessList(size_t pid);
    size_t checkWaitPidDeadlock(size_t pid_to_wait, size_t waited_by);

    char const **progs_;
    uint32 progs_running_;
    Mutex counter_lock_;
    Condition all_processes_killed_;
    static ProcessRegistry* instance_;

    ustl::map<size_t, Condition*> waitpid_map_;
    ustl::map<size_t, ustl::vector<size_t>> wait_pid_map_helper_; // MAP< PID_TO_WAIT, vector<WAITED_BY_TID>>

    size_t pid_counter_;
    size_t last_dead_pid_;
    Mutex pid_counter_lock_;

    // TODO rethink this if a more sophisticated approach for handing out pid's is implemented
    ustl::map<size_t, UserProcess*> process_list_;
    Mutex process_list_lock_;
};

