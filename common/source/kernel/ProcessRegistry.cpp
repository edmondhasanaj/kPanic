#include <mm/KernelMemoryManager.h>
#include "ProcessRegistry.h"
#include "Scheduler.h"
#include "UserProcess.h"
#include "kprintf.h"
#include "VfsSyscall.h"


ProcessRegistry* ProcessRegistry::instance_ = 0;

ProcessRegistry::ProcessRegistry(FileSystemInfo *root_fs_info, char const *progs[]) :
    Thread(root_fs_info, "ProcessRegistry", Thread::KERNEL_THREAD), waitpid_map_lock_("ProcessRegistry::waitpid_map_lock_"),
    any_process_died_(&waitpid_map_lock_, "ProcessRegistry::any_process_died"), progs_(progs), progs_running_(0),
    counter_lock_("ProcessRegistry::counter_lock_"), all_processes_killed_(&counter_lock_, "ProcessRegistry::all_processes_killed_"),
    pid_counter_(0), last_dead_pid_(0), pid_counter_lock_("pid_counter_lock"), process_list_lock_("process_list_lock")
{
  instance_ = this; // instance_ is static! -> Singleton-like behaviour
//  pid_list_ = new ustl::vector<size_t>;
//  pid_list_lock_ = new Mutex("pid_list_lock");
}

ProcessRegistry::~ProcessRegistry()
{
//  delete pid_list_;
//  delete pid_list_lock_;
}

ProcessRegistry* ProcessRegistry::instance()
{
  return instance_;
}

void ProcessRegistry::Run()
{
  if (!progs_ || !progs_[0])
    return;

  debug(PROCESS_REG, "mounting userprog-partition \n");

  VfsSyscall::mkdir("/usr", 0);
  debug(PROCESS_REG, "mkdir /usr\n");
  VfsSyscall::mount("idea1", "/usr", "minixfs", 0);
  debug(PROCESS_REG, "mount idea1\n");

  KernelMemoryManager::instance()->startTracing();

  for (uint32 i = 0; progs_[i]; i++)
  {
    createProcess(progs_[i]);
  }

  counter_lock_.acquire();

  while (progs_running_)
    all_processes_killed_.wait();

  counter_lock_.release();

  debug(PROCESS_REG, "unmounting userprog-partition because all processes terminated \n");

  VfsSyscall::umount("/usr", 0);

  Scheduler::instance()->printStackTraces();

  Scheduler::instance()->printThreadList();

  kill();
}

void ProcessRegistry::processExit(size_t pid)
{
  eraseFromProcessList(pid);

  counter_lock_.acquire();

  if (--progs_running_ == 0)
    all_processes_killed_.signal();

  counter_lock_.release();
}

void ProcessRegistry::processStart(UserProcess* proc)
{
  insertIntoProcessList(proc);

  counter_lock_.acquire();
  ++progs_running_;
  counter_lock_.release();
}

size_t ProcessRegistry::processCount()
{
  MutexLock lock(counter_lock_);
  return progs_running_;
}

void ProcessRegistry::createProcess(const char* path)
{
  debug(PROCESS_REG, "create process %s\n", path);
  UserProcess* new_process = new UserProcess(getNewPID(), path, new FileSystemInfo(*working_dir_));

  if(new_process == nullptr || new_process->getFirstThread() == nullptr) {
    debug(PROCESS_REG, "could not create userprocess %s\n", path);
    return;
  }

  debug(PROCESS_REG, "created usernew_process %s\n", path);
  Scheduler::instance()->addNewThread(new_process->getFirstThread());
  debug(PROCESS_REG, "added thread %s\n", path);
}

size_t ProcessRegistry::getNewPID() {
  pid_counter_lock_.acquire();
  size_t temp = pid_counter_++;
  pid_counter_lock_.release();
  return temp;
}

UserProcess* ProcessRegistry::createProcessOnFork() {
  debug(PROCESS_REG, "create process on fork()\n");

  auto thread = (UserThread*)currentThread;
  auto process = thread->getParentProc();

  auto new_process = new UserProcess(*process);

  debug(PROCESS_REG, "UserProcess at: %p\n", new_process);

  if(new_process == nullptr) {
    debug(PROCESS_REG, "could not create userprocess on fork\n");
    return nullptr;
  }

  debug(PROCESS_REG, "created userprocess on fork\n");
  return new_process;
}

void ProcessRegistry::insertIntoProcessList(UserProcess *proc) {
  process_list_lock_.acquire();
  process_list_[proc->getPid()] = proc;
  process_list_lock_.release();
}

void ProcessRegistry::eraseFromProcessList(size_t pid) {
  process_list_lock_.acquire();
  if(pidInProcessList(pid)) {
    process_list_.erase(pid);
  }
  process_list_lock_.release();
}

bool ProcessRegistry::pidInProcessList(size_t pid) {
  if(process_list_.find(pid) == process_list_.end())
    return false;

  return true;
}

UserProcess *ProcessRegistry::getUserProcessFromPid(size_t pid) {
  UserProcess* temp = nullptr;

  process_list_lock_.acquire();
  if(pidInProcessList(pid)) {
    temp = process_list_[pid];
  }
  process_list_lock_.release();

  return temp;
}

void ProcessRegistry::addToWaitPIDMap(size_t pid)
{
  waitpid_map_lock_.acquire();
  assert(waitpid_map_.find(pid) == waitpid_map_.end());
  ustl::string cond_var_name = "[waitpid]: waiting for pid: " + ustl::to_string(pid);
  waitpid_map_[pid] = new Condition(&waitpid_map_lock_, cond_var_name);
  wait_pid_map_helper_[pid] = {};
  waitpid_map_lock_.release();
}

void ProcessRegistry::removeFromWaitPIDMap(size_t pid)
{
  waitpid_map_lock_.acquire();
  assert(waitpid_map_.find(pid) != waitpid_map_.end());
  waitpid_map_.at(pid)->broadcast();
  any_process_died_.broadcast();
  last_dead_pid_ = pid;
  delete(waitpid_map_.at(pid));
  waitpid_map_.erase(pid);
  wait_pid_map_helper_.erase(pid);
  waitpid_map_lock_.release();
}

Condition* ProcessRegistry::getCondToWait(size_t pid)
{
  Condition* ret = NULL;
  size_t current_pid = ((UserThread*)currentThread)->getParentProc()->getPid();
  waitpid_map_lock_.acquire();
  size_t deadlock = checkWaitPidDeadlock(pid, current_pid);
  if(deadlock == -1U)
  {
    debug(PROCESS_REG, "***DEADLOCK*** DETECTED WHEN PID %ld TRIES WAITING FOR %ld\n", current_pid, pid);
    waitpid_map_lock_.release();
    return NULL;
  }
  assert(deadlock == 0);
  if(waitpid_map_.find(pid) != waitpid_map_.end())
  {
    ret = waitpid_map_.at(pid);
    wait_pid_map_helper_.at(current_pid).push_back(pid);
  }
  waitpid_map_lock_.release();
  return ret;
}

size_t ProcessRegistry::getLastDeadPID()
{
  return last_dead_pid_;
}

size_t ProcessRegistry::checkWaitPidDeadlock(size_t pid_to_wait, size_t waited_by) // Should be called when waitpid_map_lock_ is acquired
{
  if(wait_pid_map_helper_.at(pid_to_wait).size() == 0)
  {
    return 0;
  }
  for(size_t pid : wait_pid_map_helper_.at(pid_to_wait))
  {
    if(pid == waited_by || (checkWaitPidDeadlock(pid, waited_by) == -1U))
    {
      return -1U;
    }
  }
  return 0;
}

/*size_t ProcessRegistry::getLowestFreePid() {

  for(size_t counter = 0; counter < pid_list_->size(); counter++) {
    auto iterator = ustl::find(pid_list_->begin(), pid_list_->end(), counter);

void ProcessRegistry::eraseFromProcessList(size_t pid) {
  process_list_lock_.acquire();
  if(pidInProcessList(pid)) {
    process_list_.erase(pid);
  }
  process_list_lock_.release();
}

bool ProcessRegistry::pidInProcessList(size_t pid) {
  if(process_list_.find(pid) == process_list_.end())
    return false;

  return true;
}

UserProcess *ProcessRegistry::getUserProcessFromPid(size_t pid) {
  UserProcess* temp = nullptr;

  process_list_lock_.acquire();
  if(pidInProcessList(pid)) {
    temp = process_list_[pid];
  }
  process_list_lock_.release();

  return temp;
}
*/