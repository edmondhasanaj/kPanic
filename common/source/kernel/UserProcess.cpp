#include <PageManager.h>
#include <ArchInterrupts.h>
#include <COWManager.h>
#include "ProcessRegistry.h"
#include "UserProcess.h"
#include "kprintf.h"
#include "Console.h"
#include "Loader.h"
#include "VfsSyscall.h"
#include "File.h"
#include "ArchThreads.h"
#include "offsets.h"
#include "UThreadManager.h"
#include "syscall-definitions.h"
#include "FileDescriptor.h"

UserProcess::UserProcess(size_t pid, ustl::string filename, FileSystemInfo *fs_info, uint32 terminal_number) :
    pid_(pid),
    filename_(filename), fs_info_(fs_info), terminal_number_(terminal_number), next_tid_(0),
    thread_list_(), user_stack_list_(), thread_list_lock_("thread_list_lock"), user_stack_list_lock_("user_stack_list_lock"),
    waiting_list_(), ret_values_(), waiters_lock_("waiters_lock"), called_exit_(false), accumulated_incs_(0), fd_num_(3), fds_lock_("locking local fd"), 
    pipes_lock_("locking pipes"), tid_list_()
{
  ProcessRegistry::instance()->processStart(this); //should also be called if you fork a process
  fd_ = addLocalFD(VfsSyscall::open(filename, O_RDONLY));

  if(getGlobalFD(fd_) != -1U) {
    loader_ = new Loader(getGlobalFD(fd_));
  }

  if (!loader_ || !loader_->loadExecutableAndInitProcess())
  {
    debug(USERPROCESS, "Error: loading %s failed!\n", filename.c_str());
    deleteResources(true);
    ProcessRegistry::instance()->processExit(pid_);
    return;
  }

  debug(USERPROCESS, "ctor: Done loading %s\n", filename.c_str());

  if (main_console->getTerminal(terminal_number_))
    setTerminal(main_console->getTerminal(terminal_number_));


  // map page for arguments and environment variables
  debug(USERPROCESS, "Mapping args_seg_addr_ ...\n");
  args_seg_addr_ = ARGS_SEGMENT_START;
  size_t vpn = args_seg_addr_;
  size_t ppn = PageManager::instance()->allocPPN();
  args_ppn_ = ppn;
  bool mapped = loader_->arch_memory_.mapPage(vpn / PAGE_SIZE - 1, ppn, 1);
  assert(mapped && "Virtual page for arguments and environment variables was already mapped! - This should never happen!");
  // TODO: free resources in case of error
  debug(USERPROCESS, "Mapping done! args_seg_addr_: %zx\n", args_seg_addr_);


  if(addNewThread(filename, loader_->getEntryFunction(), false) == nullptr)
  {
    deleteResources(true);
    ProcessRegistry::instance()->processExit(pid_);
    return;
  }
  ProcessRegistry::instance()->addToWaitPIDMap(pid_);
}

UserProcess::UserProcess(const UserProcess &proc) :
    fd_(VfsSyscall::open(proc.getFilename(), O_RDONLY)), filename_(proc.getFilename()),
    thread_list_(), user_stack_list_(), thread_list_lock_("thread_list_lock"), user_stack_list_lock_("user_stack_list_lock"),
    waiting_list_(), ret_values_(), waiters_lock_("waiters_lock"), called_exit_(false), accumulated_incs_(0), fds_lock_("locking local fd"),
    pipes_lock_("locking pipes")
{
  assert((currentThread->getType() == Thread::USER_THREAD) && "can't call fork on a kernelthread");

  debug(USERPROCESS, "Creating process with name: %s \n", filename_.c_str());
  pid_ = ProcessRegistry::instance()->getNewPID();

  // pid has to be set before this function can be called!
  ProcessRegistry::instance()->processStart(this);
  filename_ = proc.getFilename();
  fd_num_ = proc.getFdNum();
  fd_ = proc.getOrigLocalFD();
  ustl::map<int, int> copy_fds = proc.getLocalFDs();
  for(auto entry : copy_fds)
  {
    File* f = new File(*VfsSyscall::getFileDescriptor(entry.second)->getFile());
    auto copy_fd = new FileDescriptor(f);
    FileDescriptor::add(copy_fd);
    fds_[entry.first] = (int)copy_fd->getFd();
  }
  ustl::map<ustl::pair<int, int>, RingBuffer<char>*> copy_pipes = proc.getPipes();
  for(auto entry : copy_pipes)
  {
    pipes_[entry.first] = entry.second;
    entry.second->incProcCount();
  }
  /*
  for(auto x : fds_)
  {
    debug(USERPROCESS, "NEW________________________________________[%ld] %d -> %d\n",pid_, x.first, x.second);
  }
  */
  fds_[fd_] = VfsSyscall::open(filename_, O_RDONLY);
  loader_ = new Loader(getGlobalFD(fd_));
  fs_info_ = new FileSystemInfo(*proc.getFsInfo());

  if (!loader_ || !loader_->loadExecutableAndInitProcess())
  {
    debug(USERPROCESS, "Error: loading %s failed!\n", filename_.c_str());
    deleteResources(true);
    ProcessRegistry::instance()->processExit(pid_);
    return;
  }
  debug(USERPROCESS, "ctor: Done loading %s\n", filename_.c_str());

  terminal_number_ = proc.getTerminalNumber();

//  proc.getLoader()->arch_memory_.copyPagesToNewArchMem(loader_->arch_memory_);
  proc.getLoader()->arch_memory_.copyPagesToNewArchMemCOW(loader_->arch_memory_, proc.getPid(), getPid());

  auto new_thread = addNewThread("", nullptr, true);

  if(new_thread == nullptr)
  {
    debug(USERPROCESS, "ctor: Could not create UserThread in %s -> ending Process\n", filename_.c_str());
    deleteResources(true);
    ProcessRegistry::instance()->processExit(pid_);
    return;
  }

  if (main_console->getTerminal(terminal_number_))
    setTerminal(main_console->getTerminal(terminal_number_));

  debug(USERPROCESS, "Successfully constructed Process with pid: %zu\n", pid_);
  
  ProcessRegistry::instance()->addToWaitPIDMap(pid_);
}

UserProcess::~UserProcess()
{
  assert((thread_list_.empty() && user_stack_list_.empty()) &&
         "Tried to end Process with active Threads");

  assert(Scheduler::instance()->isCurrentlyCleaningUp());

  for(auto fd : fds_)
  {
    assert(getGlobalFD(fd.first) != -1U && "No mapped global descriptor");
    VfsSyscall::close(fd.second);
  }
  
  for(auto pipe : pipes_)
  {
    pipe.second->decProcCount();
    if(pipe.second->getProcCount() == 0)
    {
      delete(pipe.second);
    }
  }
  
  COWManager::instance()->eraseProcessFromCOWMap(this);

    

  deleteResources(true);

  debug(USERPROCESS, "Ending Process with pid: %zu\n", pid_);
  ProcessRegistry::instance()->processExit(pid_);
  ProcessRegistry::instance()->removeFromWaitPIDMap(pid_);
}


size_t UserProcess::getPid() const {
  return pid_;
}

Loader* UserProcess::getLoader() const
{
  return loader_;
}

ustl::string UserProcess::getFilename() const {
  return filename_;
}

FileSystemInfo* UserProcess::getFsInfo() const {
  return fs_info_;
}

uint32 UserProcess::getTerminalNumber() const {
  return terminal_number_;
}

Terminal* UserProcess::getTerminal() const
{
  if (my_terminal_)
    return my_terminal_;
  else
    return (main_console->getActiveTerminal());
}

uint64_t UserProcess::getAccTime() const
{
  return accumulated_incs_;
}

void UserProcess::setTerminal(Terminal *my_term)
{
  my_terminal_ = my_term;
}

UserThread* UserProcess::getFirstThread() {
  acquireThreadsListLock();

  if (checkIfThreadListIsEmpty()) {
    releaseThreadsListLock();
    return nullptr;
  }

  auto* to_return = thread_list_.begin()->second;
  releaseThreadsListLock();

  return to_return;
}

UserThread* UserProcess::addNewThread(ustl::string name, void *entry_point, bool on_fork)
{
  acquireThreadsListLock();
  UserThread* new_thread = internalAddNewThread(name, entry_point, on_fork);
  releaseThreadsListLock();

  return new_thread;
}

void UserProcess::onThreadDestroyed(size_t id)
{
  //if the thread list is null -> exit was called before
  if(thread_list_.empty()) {
    delete this;
    return;
  }

  acquireThreadsListLock();
  assert(thread_list_.find(id) != thread_list_.end() && "The thread that was destroyed doesn't belong to this process");

  eraseThreadInformation(id);

  bool isEmpty = checkIfThreadListIsEmpty();
  releaseThreadsListLock();

  if(isEmpty)
  {
    delete this;
  }
}

uint64 UserProcess::ASLRStackManager(size_t tid)
{
  user_stack_list_lock_.acquire((pointer)this);

//  if(isThreadTidInThreadList(tid)) return -1; // Would prevent execv from restructuring current thread

  uint64 new_start = 0;
  uint64 new_end   = 0;

  do { // rand new addresses till a valid addr is found
    new_start = mersenneTwister();
    new_start = new_start % (STACK_SPACE_START);
    new_start = (new_start >> 12) << 12;       // page aligned

    if (new_start < STACK_SPACE_END + STACK_MAX_SIZE)
    {
      new_start = 0;
      continue;
    }

    new_end = new_start - STACK_MAX_SIZE;

    for(auto elem : user_stack_list_) // check if new stack is in conflict with others
    {
      UserStackInfo s2check = elem.second;

      // checks if the distance to another stack is greater PAGE_SIZE
      // checks if the start and end address lie out of another stack
      if(new_start <  s2check.start_address_ + PAGE_SIZE &&
         new_start >  s2check.end_address_   - PAGE_SIZE &&
         new_end   <  s2check.start_address_ + PAGE_SIZE &&
         new_end   >  s2check.end_address_   - PAGE_SIZE)
      {
        new_start = 0;
      }
    }
  } while(new_start == 0);

  user_stack_list_[tid] = {new_start, new_end};
  //((UserThread*)currentThread)->setStackPages(1); // ERROR: This would set the pages of the shell-thread, since the shell is the currentThread(), but not the pages of the UserThread, which we want to create!!!

  user_stack_list_lock_.release((pointer)this);

  assert(new_start <= STACK_SPACE_START && "FAILED: new_start <= stack_space_start");
  assert(new_end >= STACK_SPACE_END && "FAILED: new_end >= stack_space_end");

  return new_start;
  /**
   *;      PAGE_SIZE  = 4kiB
   *;      STACK_MAX_SIZE = 2048 entries = 2 kiB of memory
   *;
   *;      0x0000_8000_0000_0000 = USER_BREAK
   *;      |
   *;      |  + PAGE_SIZE
   *;      |  |               ← new_start <  s2check.start_ + PAGE_SIZE &&
   *;      |  s2check.start_
   *;      |  |
   *;      |  |
   *;      |  s2check.end_
   *;      |  |               ← new_start >  s2check.end_   - PAGE_SIZE &&
   *;      |  - PAGE_SIZE
   *;      |
   *;      0000_0000_0000_0000 = END_OF_USERSPACE
   */
}

void UserProcess::addUserStackInformation(size_t tid, uint64 stack_start, uint64 stack_end) {
  user_stack_list_lock_.acquire();

  if(isThreadTidInUserStackList(tid))
  {
    user_stack_list_lock_.release();
    return;
  }

  user_stack_list_[tid] = {stack_start, stack_end};

  user_stack_list_lock_.release();
}

size_t UserProcess::createThread(pthread_t* thread, const pthread_attr_t* attr, void (*libc_exec)(void*(*start_routine)(void*), void*),
                                 void*(*start_routine)(void*),
                                 void* arg)
{
  debug(THREAD, "T[%ld] pthread_create Request\n", currentThread->getTID());

  acquireThreadsListLock();
  UserThread* new_thread = internalAddNewThread("dynamic_thread", (void*)libc_exec, false);

  //OUT OF MEM - SAFE TO RETURN HERE
  if(new_thread == nullptr) {
    releaseThreadsListLock();
    return -1ULL;
  }

  new_thread->user_registers_->rdi = (uint64)start_routine;
  new_thread->user_registers_->rsi = (uint64)arg;

  //TODO: Add attributes later
  attr = nullptr;
  assert(attr == nullptr);

  //New thread can't be destroyed without this lock being released, so we're ok
  Scheduler::instance()->addNewThread(new_thread);
  auto newTID = new_thread->getTID();
  releaseThreadsListLock();

  *thread = newTID;

  return 0;
}

void UserProcess::exitThread(void *retval)
{
  auto* current = static_cast<UserThread*>(currentThread);
  pthread_t tid = current->getTID();
  assert(current != nullptr && "The current thread is nulltpr.\n");
  assert(current->getParentProc() == this && "Exit thread is corrupted.\n");
  debug(THREAD, "T[%ld] pthread_exit Request\n", tid);

  if(waiters_lock_.heldBy() != 0x0)
  {
    debug(THREAD, "Waiters lock is busy by: %ld\n", waiters_lock_.heldBy()->getTID());
  }

  acquireWaitersLock();
  if(ret_values_.find(tid) != ret_values_.end() || current->wasReallyExited())
  {
    debug(THREAD, "Thread is already exited...\n");
    releaseWaitersLock();
    return;
  }

  debug(THREAD, "Thread %ld is writing the return value %p\n", tid, retval);
  ret_values_.insert(ustl::make_pair(tid, retval));

  if(waiting_list_.find(tid) != waiting_list_.end())
  {
    auto waiter = waiting_list_.at(tid);
    debug(THREAD, "Thread %ld is waiting for us, so alert it.\n", waiter.waiter_);
    waiter.cond_->signal();
  }

  debug(THREAD, "Marking the thread as really exited at this point...\n");
  current->setReallyExited();
  releaseWaitersLock();

  current->exit(retval);

  debug(THREAD, "When exiting, this is currently scheduled, so yield\n");
  ArchInterrupts::enableInterrupts();
  Scheduler::instance()->yield();
}

size_t UserProcess::joinThread(pthread_t caller, pthread_t tid, void** retval)
{
  debug(THREAD, "[T%ld] pthread_join Request with TID=%ld\n", caller, tid);

  if(caller == tid)
  {
    debug(JOIN, "[T%ld] Thread is joining itself.\n", caller);
    return -1ULL;
  }

  acquireWaitersLock();

  if(ret_values_.find(tid) != ret_values_.end()) {
    void *rv = ret_values_.at(tid);
    ret_values_.erase(tid);

    debug(JOIN, "[T%ld] Thread %ld has already finished with ret value %p\n", caller, tid, rv);
    releaseWaitersLock();

    if (retval != nullptr)
    {
      *retval = rv;
    }

    return 0;
  }

  acquireThreadsListLock();
  UserThread* target = nullptr;
  if(!getThread(tid, &target) || target->wasReallyExited())
  {
    debug(JOIN, "[T%ld] The requested TID = %ld is not valid", caller, tid);
    releaseThreadsListLock();
    releaseWaitersLock();

    return -1ULL;
  }
  releaseThreadsListLock();

  if (waiting_list_.find(tid) != waiting_list_.end())
  {
    debug(JOIN, "[T%ld] Multiple joins on a single target thread detected\n", caller);
    releaseWaitersLock();
    return -1ULL;
  }

  if(joinDeadlock(caller, tid))
  {
    debug(JOIN, "[T%ld] Preventing join to not cause a deadlock\n", caller);
    releaseWaitersLock();
    return -1ULL;
  }

  //Wait for thread
  debug(THREAD, "[T%ld] Sleeping until TID=%ld finishes\n", caller, tid);

  ustl::string cond_var_name = "waiting for tid: " + ustl::to_string(tid);
  auto* cond = new Condition(&waiters_lock_, cond_var_name);
  WaitingInfo waiter = {.waiter_ = caller, .cond_ = cond};
  waiting_list_.insert(ustl::pair<pthread_t, WaitingInfo>(tid, waiter));

  cond->wait();

  debug(JOIN, "[T%ld] Thread %ld finished\n", caller, tid);
  assert(waiting_list_.find(tid) != waiting_list_.end() && "Smth was erased from the waiters list. Locking problem?\n");
  assert(ret_values_.find(tid) != ret_values_.end() && "We woke up but there is no return value available for us.\n");

  void* rv = ret_values_.at(tid);

  waiting_list_.erase(tid);
  ret_values_.erase(tid);

  releaseWaitersLock();

  if (retval != nullptr)
  {
    *retval = rv;
  }

  delete cond;

  return 0;
}

size_t UserProcess::cancelThread(pthread_t id)
{
  debug(THREAD, "T[%ld] pthread_cancel Request with target = %ld\n", currentThread->getTID(), id);

  acquireThreadsListLock();
  UserThread *target = nullptr;
  if (!getThread(id, &target))
  {
    releaseThreadsListLock();
    return -1ULL;
  }

  if(!target->isCancelableState())
  {
    releaseThreadsListLock();
    return -2ULL;
  }

  debug(THREAD, "Cancellation Request successfully received...\n");
  target->receiveCancelRequest();
  releaseThreadsListLock();
  return 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
/// PRIVATE MEMBERS
////////////////////////////////////////////////////////////////////////////////////////////////////////

void UserProcess::eraseStackListEntry(size_t tid)
{
  if(isThreadTidInUserStackList(tid))
    user_stack_list_.erase(tid);
}

uint64 UserProcess::getArgsStartAddr()
{
  return args_seg_addr_;
}

size_t UserProcess::allocNextFreeTID() {
  while (isThreadTidInThreadList(next_tid_))
  {
    next_tid_++;
  }

  //increment after returning
  return next_tid_++;
}

/**
;      PAGE_SIZE  = 4kiB
;      STACK_MAX_SIZE = 2048 entries = 2 kiB of memory
;
;      0x0000_8000_0000_0000 = USER_BREAK
;      |
;      |  + PAGE_SIZE
;      |  |               ← new_start <  s2check.start_ + PAGE_SIZE &&
;      |  s2check.start_
;      |  |
;      |  |
;      |  s2check.end_
;      |  |               ← new_start >  s2check.end_   - PAGE_SIZE &&
;      |  - PAGE_SIZE
;      |
;      0000_0000_0000_0000 = END_OF_USERSPACE
*/

// PRIVATE MEMBERS

void UserProcess::eraseThreadInformation(size_t tid) {
  if(isThreadTidInThreadList(tid)) {
    thread_list_.erase(tid);
  }

  if(isThreadTidInUserStackList(tid)) {
    user_stack_list_.erase(tid);
  }
}

bool UserProcess::isThreadTidInThreadList(size_t tid) {
  if (thread_list_.find(tid) == thread_list_.end())
    return false;

  return true;
}

bool UserProcess::isThreadTidInUserStackList(size_t tid) {
  if (user_stack_list_.find(tid) == user_stack_list_.end())
    return false;

  return true;
}

bool UserProcess::checkIfThreadListIsEmpty() {
  return thread_list_.empty();
}

UserThread* UserProcess::internalAddNewThread(ustl::string name, void *entry_point, bool on_fork)
{
  if(called_exit_) {
    debug(USERPROCESS, "No new Threads can be added when exit was already called!\n");
    return nullptr;
  }

  UserThread* new_thread;
  if(!on_fork)
  {
    size_t tid = allocNextFreeTID();
    ustl::string thread_name = name + " - pid: " + ustl::to_string(pid_) + " - tid: " + ustl::to_string(tid);
    new_thread = new UserThread(fs_info_, thread_name, Thread::USER_THREAD, this, tid, entry_point);
  }
  else
  {
    new_thread = new UserThread((UserThread*)currentThread, this, allocNextFreeTID());
  }

  if(new_thread != nullptr)
  {
    thread_list_[new_thread->getTID()] = new_thread;
  }

  return new_thread;
}

bool UserProcess::getThread(size_t tid, UserThread **ret) {
  if (!isThreadTidInThreadList(tid))
  {
    return false;
  }

  *ret = thread_list_.find(tid)->second;
  return true;
}

void UserProcess::kill() {
  debug(EXIT, "T[%ld] Exit initiated\n", currentThread->getTID());

  // exit should only be called once
  if(called_exit_) {
    debug(EXIT, "Exit already called in UserProcess::kill (tid: %lu being killed)\n", currentThread->getTID());
    ((UserThread*)currentThread)->exit(nullptr);
    Scheduler::instance()->yield();
  }

  // setting this to true also blocks new threads from being created
  called_exit_ = true;

  UserProcess* current_proc = static_cast<UserThread*>(currentThread)->getParentProc();
  UserThread* thread_to_del_last = nullptr;

  // aquire all current tid's
  tid_list_.clear();
  acquireThreadsListLock();
  for(auto& pair : thread_list_) {
    auto* thread = pair.second;

    if(thread == currentThread) {
      thread_to_del_last = thread;
      continue;
    }

    tid_list_.push_back(thread->getTID());
  }
  releaseThreadsListLock();


  // cancel and join all the threads until there are no more
  for(auto it = tid_list_.begin(); it != tid_list_.end();) {
    debug(EXIT, "[T%ld] Init cancellation request for target.\n", *it);
    size_t cancel_ret = current_proc->cancelThread(*it);
    // the thread was destroyed somewhere else
    if(cancel_ret == -1ULL)
    {
      it = tid_list_.erase(it);
      continue;
    }

    it++;
  }

  for(auto it = tid_list_.begin(); it != tid_list_.end(); it++) {
    current_proc->joinThread(currentThread->getTID(), *it, nullptr);
    debug(EXIT, "[T%ld] Joined with the cancelled thread now.\n", *it);
  }

  deleteResources(false);

  debug(USERPROCESS, "Thread who called exit -> tid: %lu\n", thread_to_del_last->getTID());

  thread_to_del_last->exit(nullptr);
  Scheduler::instance()->yield();
}

void UserProcess::deleteResources(bool delete_loader) {
  if (delete_loader) {
    if (loader_ != nullptr) {
      delete loader_;
      loader_ = nullptr;
    }
  }
}

void UserProcess::acquireThreadsListLock()
{
  thread_list_lock_.acquire();
  //debug(THREAD, "ACQUIRE THREAD LIST LOCK\n");
}
void UserProcess::releaseThreadsListLock()
{
  thread_list_lock_.release();
  //debug(THREAD, "RELEASE THREAD LIST LOCK\n");
}
void UserProcess::acquireWaitersLock()
{
  waiters_lock_.acquire();
  //debug(THREAD, "ACQUIRE WAITERS LIST LOCK\n");
}

void UserProcess::releaseWaitersLock()
{
  waiters_lock_.release();
  //debug(THREAD, "RELEASE WAITERS LIST LOCK\n");
}

UserThread* UserProcess::addrIsWithinAnyUserStack(size_t address)
{
//  debug(USERPROCESS, "[addrIsWithinAnyUserStack] Is address %14zx within any user stack?\n", address);

  if (address > STACK_SPACE_START ||
      address < STACK_SPACE_END)
  {
    debug(USERPROCESS, "[addrIsWithinAnyUserStack] Nope, address is not within StackSpace!\n");
    return NULL;
  }

  ustl::map<size_t,UserThread*>::iterator it;
  UserThread* thread_of_given_stack_addr = NULL;

  acquireThreadsListLock();
  for (it = thread_list_.begin(); it != thread_list_.end(); it++)
  {
    uint64 stack_start_addr = it->second->getUserStackStartAddr();
    if (address <= stack_start_addr &&
        address >  stack_start_addr - STACK_MAX_SIZE)
    {
      // address is within local stack space
      thread_of_given_stack_addr = it->second;
      break;
    }
  }
  releaseThreadsListLock();

  if (thread_of_given_stack_addr)
    debug(USERPROCESS, "[addrIsWithinAnyUserStack] Yes!\n");
  else
    debug(USERPROCESS, "[addrIsWithinAnyUserStack] No related stack found!\n");

  return thread_of_given_stack_addr;
}

//NOT THREAD SAFE. What happens if thread while used? Look at florian and how he did it
void UserProcess::killThreadsSiblings(size_t tid)
{
  debug(USERPROCESS, "killThreadsSiblings: Thread %zu is gonna rogue and kills his siblings!\n", tid);
  ustl::map<size_t,UserThread*>::iterator it;

  acquireThreadsListLock();
  for (it = thread_list_.begin(); it != thread_list_.end(); it++)
  {
    if (it->first != tid)
    {
      debug(USERPROCESS, "killThreadsSiblings: pthread_cancel(%zu);", it->first);
      releaseThreadsListLock();
      UThreadManager::instance()->cancel_thread(it->first);            // stitch threads sibling
      UThreadManager::instance()->join_thread(it->first, NULL);  // wait till it bleeds out
      acquireThreadsListLock();
    }
  }
  releaseThreadsListLock();
}

size_t UserProcess::restructureProcessForExecv(ustl::string filename, pointer argv, int argc)
{
  assert((currentThread->getType() == Thread::USER_THREAD) && "Can't call execv by kernel-thread!");
  debug(USERPROCESS, "Restructure process: %s \n", filename.c_str());

  killThreadsSiblings(currentThread->getTID());

  int32 new_fd      = VfsSyscall::open(filename, O_RDONLY);
  auto  new_loader  = new Loader(new_fd);

  if (!new_loader->loadExecutableAndInitProcess())
  {
    debug(USERPROCESS, "Error: loading %s failed!\n", filename.c_str());
    delete new_loader;
    //ProcessRegistry::instance()->processExit();   // throws [BACKTRACE  ]
    return -1;
  }
  getLoader()->arch_memory_.copyArgsSegmentToNewArchMem(new_loader->arch_memory_, args_ppn_);
  debug(USERPROCESS, "Swap loaders ...\n");
  auto old_loader = loader_;
  loader_ = new_loader;

  debug(USERPROCESS, "Restructure current thread ...\n");
  ustl::string new_name = filename + " - pid: " + ustl::to_string(pid_) + " - tid: " + ustl::to_string(currentThread->getTID());

  ((UserThread*)currentThread)->restructureUserThread(new_loader, new_name);

  currentThread->user_registers_->rdi = argc;
  currentThread->user_registers_->rsi = argv;

  debug(USERPROCESS, "Delete old loader ...\n");
  delete old_loader;

  debug(USERPROCESS, "Successfully restructured Process: %zu\n", pid_);
  return 0;
}



// ==== PRIVATE MEMBERS ====
uint64_t UserProcess::readTSC()
{
  uint32_t low, high;
  asm volatile("rdtsc" : "=a"(low), "=d"(high));
  uint64_t cycles_since_reboot = (uint64_t) ((uint64_t) high << 32) | low;
  return cycles_since_reboot;
}

uint64_t UserProcess::mersenneTwister()
{
  // [Source](https://youtu.be/C82JyCmtKWg?t=260)
  //debug(USERPROCESS, "[mersenneTwister]\n");

  uint64_t x = readTSC();         // using TSC as seed
  uint64_t x_new = 0;
  uint64_t a =  378;              // multiplier
  uint64_t c = 2310;              // increment
  uint64_t m = STACK_SPACE_START; // modulus
  size_t   N = 10;                // number of passes

  for (size_t i = 0; i < N; i++)
  {
    x_new = (a * x + c) % m;
    x = x_new;
  }

  return x_new;
}

bool UserProcess::joinDeadlock(pthread_t caller, pthread_t target)
{
  pthread_t nextWaiter = caller;
  while(true)
  {
    auto *waiting_for_us = waiting_list_.find(nextWaiter);
    if (waiting_for_us != waiting_list_.end())
    {
      nextWaiter = waiting_for_us->second.waiter_;

      if(nextWaiter == target)
      {
        return true;
      }

      continue;
    }

    return false;
  }
}

void UserProcess::incAccTime(size_t time)
{
  accumulated_incs_+= (uint64_t)time;
}

ustl::map<int, int> UserProcess::getLocalFDs() const
{
  /*
  ustl::map<int, int> copy_map;
  //fds_lock_.acquire();
  for(auto entry : fds_)
  {
    auto copy_fd = new FileDescriptor(VfsSyscall::getFileDescriptor(entry.second)->getFile());
    FileDescriptor::add(copy_fd);
    copy_map[entry.first] = (int)copy_fd->getFd();
  }
  //fds_lock_.release();
  */
  return fds_;
}


size_t UserProcess::addLocalFD(int global_fd)
{
  //(flag_ & O_WRONLY)
  size_t cur_fd = 0;
  fds_lock_.acquire();
  fds_[++fd_num_] = global_fd;
  cur_fd = fd_num_;
  //debug(USERPROCESS, "________________________________________[%ld]ADDING: |%ld -> %d| \n", pid_, cur_fd, global_fd);
  fds_lock_.release();
  return cur_fd;
}

void UserProcess::removeLocalFD(int fd)
{
  //(flag_ & O_WRONLY)
  fds_lock_.acquire();
  fds_.erase(fd);
  fds_lock_.release();
  pipes_lock_.acquire();
  for(auto entry : pipes_)
  {
    if(entry.first.first == fd && entry.first.second == PIPE_FD_CLOSED)
    {
      entry.second->decProcCount();
      if(entry.second->getProcCount() == 0)
      {
        delete(entry.second);
      }
      pipes_.erase({entry.first.first, entry.first.second});
      break;
    }
    else if(entry.first.first == fd && entry.first.second != PIPE_FD_CLOSED)
    {
      entry.first.first = PIPE_FD_CLOSED;
      break;
    }
    if(entry.first.first == PIPE_FD_CLOSED && entry.first.second == fd)
    {
      entry.second->decProcCount();
      if(entry.second->getProcCount() == 0)
      {
        delete(entry.second);
      }
      pipes_.erase({entry.first.first, entry.first.second});
      break;
    } 
    else if(entry.first.first != PIPE_FD_CLOSED && entry.first.second == fd)
    {
      entry.first.second = PIPE_FD_CLOSED;
      break;
    }
  }
  pipes_lock_.release();
}

size_t UserProcess::getGlobalFD(int local_fd)
{
  //(flag_ & O_WRONLY)
  size_t glob_fd = 0;
  fds_lock_.acquire();
  /*
  for(auto x : fds_)
  {
    debug(USERPROCESS, "________________________________________[%ld] %d -> %d\n",pid_, x.first, x.second);
  }
  debug(USERPROCESS, "________________________________________ [%ld]GLOBAL_FD(%ld): get %d \n", pid_, global_fd.size(), local_fd);
  */
  glob_fd = (fds_.find(local_fd) == fds_.end()) ? -1U : fds_.at(local_fd);
  fds_lock_.release();
  return glob_fd;
}

size_t UserProcess::getOrigLocalFD() const
{
  return fd_;
}

size_t UserProcess::getFdNum() const
{
  return fd_num_;
}

size_t UserProcess::openPipe(size_t read, size_t write)
{
  //fds_lock_.acquire();
  //fds_[++fd_num_] = PIPE_FD_CODE;
  //debug(USERPROCESS, "________________________________________[%ld]\n",read);  
  *(int*)read = ++fd_num_;
  //fds_[++fd_num_] = PIPE_FD_CODE;
  *(int*)write = ++fd_num_;
  //fds_lock_.release();
  pipes_lock_.acquire();
  pipes_[{*(int*)read, *(int*)write}] = new RingBuffer<char>(PIPE_BUF_SIZE);
  pipes_.at({*(int*)read, *(int*)write})->incProcCount();
  pipes_lock_.release();
  return 0;
  //debug(USERPROCESS, "________________________________________[%ld]ADDING: |%ld -> %d| \n", pid_, cur_fd, global_fd);
}

size_t UserProcess::readFromPipe(size_t read_fd, pointer buffer, size_t count)
{
  RingBuffer<char>* to_read = NULL;
  pipes_lock_.acquire();
  for(auto entry : pipes_)
  {
    if(entry.first.first == (int)read_fd)
    {
      to_read = entry.second;
      break;
    }
  }
  if(to_read == NULL)
  {
    pipes_lock_.release();
    return -1;// NOT THE READ END OF THE PIPE
  }
  //assert(to_read != NULL && "Should not happen since the existence is checked beforehand");
  size_t character = 0;
  while (character < count && (to_read->get(((char*)buffer)[character])))
  {
    character++;
  }
  pipes_lock_.release();
  return character;
}
size_t UserProcess::writeToPipe(size_t write_fd, pointer buffer, size_t count)
{
  RingBuffer<char>* to_write = NULL;
  pipes_lock_.acquire();
  for(auto entry : pipes_)
  {
    if(entry.first.second == (int)write_fd)
    {
      to_write = entry.second;
      break;
    }
  }
  if(to_write == NULL)
  {
    pipes_lock_.release();
    return -1; // NOT THE WRITE END OF THE PIPE
  }
  //assert(to_write != NULL && "Should not happen since the existence is checked beforehand");
  size_t character = 0;
  while (character < count && (to_write->put(((char*)buffer)[character])))
  {
    //debug(USERPROCESS, "________________________________________PIPE W: %c\n", ((char*)buffer)[character]);
    character++;
  }
  pipes_lock_.release();
  return character; 
}

bool UserProcess::isPipe(int local_fd)
{
  pipes_lock_.acquire();
  for(auto entry : pipes_)
  {
    if(entry.first.first == local_fd || entry.first.second == local_fd)
    {
      pipes_lock_.release();
      return true;
    }
  }
  pipes_lock_.release();
  return false;
}

ustl::map<ustl::pair<int, int>, RingBuffer<char>*> UserProcess::getPipes() const
{
  return pipes_;
}