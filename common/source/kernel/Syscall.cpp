#include <PageManager.h>  // execv
#include "ArchThreads.h"  // fork
#include "offsets.h"
#include "Syscall.h"
#include "syscall-definitions.h"
#include "Terminal.h"
#include "debug_bochs.h"
#include "VfsSyscall.h"
#include "UserProcess.h"
#include "ProcessRegistry.h"
#include "File.h"
#include "UThreadManager.h"

size_t Syscall::syscallException(size_t syscall_number, size_t arg1, size_t arg2, size_t arg3, size_t arg4, size_t arg5)
{
  size_t return_value = 0;

  if ((syscall_number != sc_sched_yield) && (syscall_number != sc_outline)) // no debug print because these might occur very often
  {
    debug(SYSCALL, "Syscall %zd called with arguments %zd(=%zx) %zd(=%zx) %zd(=%zx) %zd(=%zx) %zd(=%zx)\n",
          syscall_number, arg1, arg1, arg2, arg2, arg3, arg3, arg4, arg4, arg5, arg5);
  }

  switch (syscall_number)
  {
    case sc_sched_yield:
      Scheduler::instance()->yield();
      break;
    case sc_createprocess:
      return_value = createprocess(arg1, arg2);
      break;
    case sc_exit:
      exit(arg1);
      break;
    case sc_write:
      return_value = write(arg1, arg2, arg3);
      break;
    case sc_read:
      return_value = read(arg1, arg2, arg3);
      break;
    case sc_open:
      return_value = open(arg1, arg2);
      break;
    case sc_close:
      return_value = close(arg1);
      break;
    case sc_outline:
      outline(arg1, arg2);
      break;
    case sc_trace:
      trace();
      break;
    case sc_pseudols:
      VfsSyscall::readdir((const char*) arg1);
      break;
    case sc_pthread_create:
      return_value =
              UThreadManager::instance()->create_thread((pthread_t*) arg1,
                                                        (pthread_attr_t*) arg2,
                                                        (void(*)(void*(*libc_start)(void*), void*)) arg3,
                                                        (void*(*)(void*)) arg4,
                                                        (void*) arg5);
      break;
    case sc_pthread_exit:
      UThreadManager::instance()->exit_thread((void*) arg1);
      break;
    case sc_pthread_join:
      return_value = UThreadManager::instance()->join_thread((pthread_t)arg1,
                                                             (void**)arg2);
      break;
    case sc_pthread_cancel:
      return_value = UThreadManager::instance()->cancel_thread((pthread_t)arg1);
      break;
    case sc_fork:
      return_value = Syscall::fork();
      break;
    case sc_clock:
      return_value = clock();
      break;
    case sc_sleep:
      return_value = sleep(arg1);
      break;
    case sc_waitpid:
      return_value = waitpid(arg1, arg2, arg3);
      break;
    case sc_pipe:
      return_value = pipe(arg1, arg2);
      break;
    case sc_execv:
      return_value = Syscall::execv(arg1, arg2);
      break;
    default:
      kprintf("Syscall::syscall_exception: Unimplemented Syscall Number %zd\n", syscall_number);
  }
  return return_value;
}

void Syscall::exit(size_t exit_code)
{
  debug(SYSCALL, "Syscall::EXIT: called, exit_code: %zd\n", exit_code);
  ((UserThread*)currentThread)->getParentProc()->kill();
}

size_t Syscall::write(size_t fd, pointer buffer, size_t size)
{
  //WARNING: this might fail if Kernel PageFaults are not handled
  if ((buffer >= USER_BREAK) || (buffer + size > USER_BREAK))
  {
    return -1U;
  }

  size_t num_written = 0;

  if (fd == fd_stdout) //stdout
  {
    debug(SYSCALL, "Syscall::write: %.*s\n", (int)size, (char*) buffer);
    kprintf("%.*s", (int)size, (char*) buffer);
    num_written = size;
  }
  else if (((UserThread*)currentThread)->getParentProc()->isPipe(fd))
  {
    num_written = ((UserThread*)currentThread)->getParentProc()->writeToPipe(fd, buffer, size);
  }
  else
  {
    auto proc = ((UserThread*)currentThread)->getParentProc();
    size_t global_fd = proc->getGlobalFD(fd);
    num_written = (global_fd == -1U) ? -1 : VfsSyscall::write(global_fd, (char*) buffer, size);
  }
  return num_written;
}

size_t Syscall::read(size_t fd, pointer buffer, size_t count)
{
  if ((buffer >= USER_BREAK) || (buffer + count > USER_BREAK))
  {
    return -1U;
  }

  size_t num_read = 0;

  if (fd == fd_stdin)
  {
    //this doesn't! terminate a string with \0, gotta do that yourself
    num_read = currentThread->getTerminal()->readLine((char*) buffer, count);
    debug(SYSCALL, "Syscall::read: %.*s\n", (int)num_read, (char*) buffer);
  }
  else if (((UserThread*)currentThread)->getParentProc()->isPipe(fd))
  {
    num_read = ((UserThread*)currentThread)->getParentProc()->readFromPipe(fd, buffer, count);
  }
  else
  {
    auto proc = ((UserThread*)currentThread)->getParentProc();
    size_t global_fd = proc->getGlobalFD(fd);
    //debug(SYSCALL, "Syscall::READ___________________________________________________ %ld\n", global_fd);
    num_read = (global_fd == -1U) ? -1 : VfsSyscall::read(global_fd, (char*) buffer, count);
  }
  return num_read;
}

size_t Syscall::close(size_t fd)
{
  auto proc = ((UserThread*)currentThread)->getParentProc();
  size_t global_fd = proc->getGlobalFD(fd);
  if (proc->isPipe(fd))
  {
    proc->removeLocalFD(fd);
    return 0;
  }
  int32 retval = (global_fd == -1U) ? -1 : VfsSyscall::close(global_fd);
  if(retval != -1)
  {
    proc->removeLocalFD(fd);
  }
  return retval;
}

size_t Syscall::open(size_t path, size_t flags)
{
  if (path >= USER_BREAK)
  {
    return -1U;
  }
  auto proc = ((UserThread*)currentThread)->getParentProc();
  int32 fd = VfsSyscall::open((char*) path, flags);
  debug(SYSCALL, "Syscall::open[%ld]: %d\n", proc->getPid(), fd);
  return proc->addLocalFD(fd);
}

void Syscall::outline(size_t port, pointer text)
{
  //WARNING: this might fail if Kernel PageFaults are not handled
  if (text >= USER_BREAK)
  {
    return;
  }
  if (port == 0xe9) // debug port
  {
    writeLine2Bochs((const char*) text);
  }
}

size_t Syscall::createprocess(size_t path, size_t sleep)
{
  // THIS METHOD IS FOR TESTING PURPOSES ONLY!
  // AVOID USING IT AS SOON AS YOU HAVE AN ALTERNATIVE!

  // parameter check begin
  if (path >= USER_BREAK)
  {
    return -1U;
  }
  auto proc = ((UserThread*)currentThread)->getParentProc();
  debug(SYSCALL, "Syscall::createprocess: path:%s sleep:%zd\n", (char*) path, sleep);
  ssize_t fd = proc->addLocalFD(VfsSyscall::open((const char*) path, O_RDONLY));
  if (fd == -1)
  {
    return -1U;
  }
  VfsSyscall::close(proc->getGlobalFD(fd));

  // parameter check end

  size_t process_count = ProcessRegistry::instance()->processCount();
  ProcessRegistry::instance()->createProcess((const char*) path);
  if (sleep)
  {
    while (ProcessRegistry::instance()->processCount() > process_count) // please note that this will fail ;)
    {
      Scheduler::instance()->yield();
    }
  }
  return 0;
}

void Syscall::trace()
{
  currentThread->printBacktrace();
}

//Mutex fork_lock("fork_lock");
size_t Syscall::fork()
{
  assert((currentThread->getType() == Thread::USER_THREAD) && "Forking can only be done by a user thread");

  auto new_process = ProcessRegistry::instance()->createProcessOnFork();

  // set the return value of the new thread to 0
  new_process->getFirstThread()->user_registers_->rax = 0;

  size_t ret_val = new_process->getPid();

  Scheduler::instance()->addNewThread(new_process->getFirstThread());

  return ret_val;
}


size_t Syscall::clock()
{
  assert((currentThread->getType() == Thread::USER_THREAD) && "Conversion to UThread will fail");
  auto sc = Scheduler::instance();
  auto uprocess = reinterpret_cast<UserThread*>(currentThread)->getParentProc();
  //uint64_t acc = uprocess->getAccTime() + (sc->getCurrentTime() - sc->getUThreadStartTime());
  //uint64_t div = (acc * 549254);
  //uint64_t by = (sc->getCurrentAVGClocksPerInterrupt() * 10);
  //size_t ret = div/by;
  //debug(SYSCALL, "Syscall::clock: FOR %ld : %s\n", uprocess->getPid(), currentThread->getName());
  //debug(SYSCALL, "Syscall::clock: acc: %ld\n", acc);
  //debug(SYSCALL, "Syscall::clock: %ld / %ld = %ld\n", div, by, ret);
  // 54.9254ms per interrupt
  return ((uprocess->getAccTime() + (sc->getCurrentTime() - sc->getUThreadStartTime())) * 549254) / (sc->getCurrentAVGClocksPerInterrupt() * 10);
}

size_t Syscall::sleep(size_t seconds)
{
  assert((currentThread->getType() == Thread::USER_THREAD) && "Conversion to UThread will fail");
  auto sc = Scheduler::instance();
  //18.2065 Hz, i.e. interrupts per second
  uint64_t rdtsc_wakeup_increment = ((uint64_t)182065 * seconds * sc->getCurrentAVGClocksPerInterrupt()) / 10000 + sc->getCurrentTime();
  debug(SYSCALL, "Syscall::sleep: TID %ld will sleep from now: %ld until  %ld\n", currentThread->getTID(), sc->getCurrentTime(), rdtsc_wakeup_increment);
  reinterpret_cast<UserThread*>(currentThread)->sleepUntil(rdtsc_wakeup_increment);
  return 0;
  // 0 if elapsed
}

size_t Syscall::waitpid(size_t pid, size_t status, size_t options)
{
  if (status >= USER_BREAK || pid == 0 || (int)pid < -1 || pid == ((UserThread*)currentThread)->getParentProc()->getPid())
  {
    return -1;
  }
  if(options == 0 || options == 4) // 0 and WEXITED 4
  {
    int *stat_ptr = (int*)status;
    if((int)pid == -1)
    {
      int ret = ((UserThread*)currentThread)->waitForAnyPid();
      if(stat_ptr != NULL)
      {
        *stat_ptr = ret == 0 ? 0 : -1;
      }
      //debug(SYSCALL, "RET __________________________________________%d\n",ret);
      return ret;
    }
    if((int)pid != -1)
    {
      int ret = ((UserThread*)currentThread)->waitForPid(pid);
      if(stat_ptr != NULL )
      {
        *stat_ptr = ret == 0 ? 0 : -1;
      }
      //debug(SYSCALL, "RET __________________________________________%d\n",ret);
      return ret;
    }
  }
  return -1; // ONLY WEXITED IMPLEMENTED
}

size_t Syscall::pipe(size_t read, size_t write)
{
  if (read >= USER_BREAK || write >= USER_BREAK)
  {
    return -1;
  }
  return ((UserThread*)currentThread)->getParentProc()->openPipe(read, write);
}

size_t Syscall::execv(pointer path, pointer args)
{
  debug(SYSCALL, "Restructuring UserThread '%s'\n", currentThread->getName());
  assert((currentThread->getType() == Thread::USER_THREAD) && "System call `execv` can only be performed by a user thread!");
  UserThread* cThread = ((UserThread*) currentThread);


  pointer* argsp = (pointer*)args;
  pointer      seg_ptr = cThread->getParentProc()->getArgsStartAddr() - sizeof(pointer);
  pointer path_seg_ptr = NULL;
  pointer args_seg_ptr = NULL;


  if (path == NULL)
  {
    debug(SYSCALL, "Error: path == NULL\n");
    return -1;
  }
  if (!strlen((char*)path))
  {
    debug(SYSCALL, "Error: path is an empty string!\n");
    return -1;
  }
  if (args == NULL)
  {
    debug(SYSCALL, "Error: args == NULL\n");
    return -1;
  }
  if (*(pointer*)args == NULL)
  {
    debug(SYSCALL, "Error: args-array must have at least one entry!\n");
    return -1;
  }
  if (!strlen((char*)*(pointer*)args))
  {
    debug(SYSCALL, "Error: First argument is an empty string!\n");
    return -1;
  }
  // TODO: check if path is valid and file exists


  debug(SYSCALL, "seg_ptr: %14zx\n", seg_ptr);
  debug(SYSCALL, "path:    %14zx '%s'\n", path, (char*)path);
  debug(SYSCALL, "args:    %14zx\n", args);
  debug(SYSCALL, "args[0]: %14zx '%s'\n", ((pointer*)args)[0], (char*)((pointer*)args)[0]);


//  debug(SYSCALL, "Copying path ...\n");
  seg_ptr -= strlen((char*)path);
  path_seg_ptr = seg_ptr;
  strncpy((char*)seg_ptr, (char*)path, strlen((char*)path));
//  debug(SYSCALL, "path_seg_ptr: %14zx: '%s'\n", path_seg_ptr, (char*)path_seg_ptr);
  seg_ptr -= sizeof(char);


//  debug(SYSCALL, "Creating args pointer array ...\n");
  size_t args_count = 0;
  while(argsp[args_count] != NULL)
  {
    if (!strlen((char*)argsp[args_count]))
    {
      debug(SYSCALL, "Error: Argument with empty string found!\n");
      return -1;
    }
    args_count++;
  }
  debug(SYSCALL, "args_count: %zu\n", args_count);
  seg_ptr -= args_count * sizeof(pointer);
  args_seg_ptr = seg_ptr;
  seg_ptr -= sizeof(char);


//  debug(SYSCALL, "Copying args strings ...\n");
  args_count = 0;
  while(argsp[args_count] != NULL)
  {
    size_t length = strlen( (char*)(argsp[args_count]) );
    seg_ptr -= length;
//    debug(SYSCALL, "str length: %zu\n", length);

    ((pointer*)args_seg_ptr)[args_count] = seg_ptr; // copy address into array
//    debug(SYSCALL, "args_seg_ptr: %14zx\n", ((pointer*)args_seg_ptr)[args_count]);

    strncpy((char*)seg_ptr, (char*)(argsp[args_count]), length); // copy string into segment
    args_count++;
    seg_ptr -= sizeof(char);
  }


  debug(SYSCALL, "Printing debug info ...\n");
//  pointer ptr  = cThread->getParentProc()->getArgsStartAddr() - sizeof(pointer);
//  for (int i = 0; i > -80; --i)
//    debug(SYSCALL, "%14zx : '%c'\n", ptr+i, ((char*)ptr)[i]);
  debug(SYSCALL, "args_seg_ptr:    %14zx\n", args_seg_ptr);
  for (size_t i = 0; i < args_count; ++i)
    debug(SYSCALL, "args_seg_ptr[%zu]: %14zx '%s'\n", i, ((pointer*)args_seg_ptr)[i], (char*)(((pointer*)args_seg_ptr)[i]));


  size_t r = cThread->getParentProc()->restructureProcessForExecv((char*)path_seg_ptr, args_seg_ptr, args_count);
  debug(SYSCALL, "Leaving Syscall.cpp\n");
  return r;
}


