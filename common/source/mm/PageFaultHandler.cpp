#include "PageManager.h"
#include <UserThread.h>
#include <COWManager.h>
#include "PageFaultHandler.h"
#include "kprintf.h"
#include "Thread.h"
#include "ArchInterrupts.h"
#include "offsets.h"
//#include "Scheduler.h"
//#include "Loader.h"
#include "Syscall.h"
#include "ArchThreads.h"
extern "C" void arch_contextSwitch();

void PageFaultHandler::enterPageFault(size_t address, bool user,
                                      bool present, bool writing,
                                      bool fetch)
{
  assert(currentThread && "You have a pagefault, but no current thread");
  //save previous state on stack of currentThread

  //debug(PAGEFAULT, "The Thread '%s' has a page fault\n", currentThread->getName());
  uint32 saved_switch_to_userspace = currentThread->switch_to_userspace_;

  currentThread->switch_to_userspace_ = 0;
  currentThreadRegisters = currentThread->kernel_registers_;

  //Cancellation logic. It is atomic at this point (interrupts are disabled)
  bool shouldCancel = false;
  if(currentThread->getType() == Thread::USER_THREAD) {
    auto thread = static_cast<UserThread*>(currentThread);

    //it's enough
    if(thread->shouldCancel() && thread->switch_to_userspace_ == 1)
    {
      shouldCancel = true;
      thread->setCancelled();
    }
  }

  ArchInterrupts::enableInterrupts();

  if(shouldCancel) {
    auto thread = static_cast<UserThread*>(currentThread);
    debug(SYSCALL, "T[%ld] CANCELLATION OF THREAD happening in PageFaultHandler()\n", thread->getTID());

    thread->getParentProc()->exitThread((void*) -1);
  }

  handlePageFault(address, user, present, writing, fetch, saved_switch_to_userspace);

  ArchInterrupts::disableInterrupts();
  currentThread->switch_to_userspace_ = saved_switch_to_userspace;
  if (currentThread->switch_to_userspace_)
    currentThreadRegisters = currentThread->user_registers_;
}

const size_t PageFaultHandler::null_reference_check_border_ = PAGE_SIZE;

inline bool PageFaultHandler::checkPageFaultIsValid(size_t address, bool user,
                                                    bool present, bool switch_to_us)
{
  //debug(PAGEFAULT, "current thread: %s\n", currentThread->getName());

  assert((user == switch_to_us) && "Thread is in user mode even though it should not be.");
  assert(!(address < USER_BREAK && currentThread->t_loader_ == 0) && "Thread accesses the user space, but has no loader.");
  assert(!(user && currentThread->user_registers_ == 0) && "Thread is in user mode, but has no valid registers.");

  if(address < null_reference_check_border_)
  {
    debug(PAGEFAULT, "Maybe you are dereferencing a null-pointer.\n");
  }
  else if(!user && address >= USER_BREAK)
  {
    debug(PAGEFAULT, "You are accessing an invalid kernel address.\n");
  }
  else if(user && address >= USER_BREAK)
  {
    debug(PAGEFAULT, "You are accessing a kernel address in user-mode.\n");
  }
  else if(present)
  {
    if(COWManager::instance()->ppnShared(address))
    {
      debug(PAGEFAULT, "pagefault is due to the page being shared\n");
      return true;
    }
    debug(PAGEFAULT, "You got a pagefault even though the address is mapped.\n");
  }
  else
  {
    // everything seems to be okay
    return true;
  }
  return false;
}

inline void PageFaultHandler::handlePageFault(size_t address, bool user,
                                              bool present, bool writing,
                                              bool fetch, bool switch_to_us)
{
  if (PAGEFAULT & OUTPUT_ENABLED)
    kprintfd("\n");
  debug(PAGEFAULT, "Address: %18zx - Thread %zu: %s (%p)\n",
        address, currentThread->getTID(), currentThread->getName(), currentThread);
  debug(PAGEFAULT, "Flags: %spresent, %s-mode, %s, %s-fetch, switch to userspace: %1d\n",
        present ? "    "        : "not ",
        user    ? "  user"      : "kernel",
        writing ? "writing"     : "reading",
        fetch   ? "instruction" : "    operand",
        switch_to_us);
  //Uncomment the line below if you want to have detailed information about the thread registers.
  //ArchThreads::printThreadRegisters(currentThread, false);

  if (checkPageFaultIsValid(address, user, present, switch_to_us))
  {
    UserThread* thread_of_stack_addr = ((UserThread*)currentThread)->getParentProc()->addrIsWithinAnyUserStack(address);
    if(COWManager::instance()->ppnShared(address)) 
    {
      COWManager::instance()->handleCOWPageFault(address);
    }
    else if (thread_of_stack_addr != NULL) 
    {
      increaseUserStackSize(thread_of_stack_addr);
    }
    else 
    {
      currentThread->t_loader_->loadPage(address);
    }
    /*
    // the page is marked copy on write
    if(COWManager::instance()->ppnShared(address)) {
      COWManager::instance()->handleCOWPageFault(address);
    }
    else if (addrIsWithinThisStackSpace(address)) {
      increaseUserStackSize();
    }
    else {
      currentThread->loader_->loadPage(address);
    }

    //debug(PAGEFAULT, "Page fault IS valid.\n");
    //debug(PAGEFAULT, "handlePageFault: Thread '%s' has a page fault\n", currentThread->getName());

    //if (addrIsWithinThisStackSpace(address))
    UserThread* thread_of_stack_addr = ((UserThread*)currentThread)->getParentProc()->addrIsWithinAnyUserStack(address);
    if (thread_of_stack_addr != NULL)
      increaseUserStackSize(thread_of_stack_addr);
    else
      currentThread->t_loader_->loadPage(address);

    */
  }
  else
  {
    //debug(PAGEFAULT, "Page fault is NOT valid!\n");
    // the page-fault seems to be faulty, print out the thread stack traces
    ArchThreads::printThreadRegisters(currentThread, true);
    currentThread->printBacktrace(true);
    if (currentThread->t_loader_)
      Syscall::exit(9999);
    else
      currentThread->kill();
  }
  debug(PAGEFAULT, "Page fault handling finished for Address: %18zx.\n", address);
}

bool PageFaultHandler::addrIsWithinThisStackSpace(size_t address)
{
  uint64 stack_start_addr = ((UserThread*)currentThread)->getUserStackStartAddr();

  if (address <= stack_start_addr && address > stack_start_addr - STACK_MAX_SIZE)
    return true; // address is within StackSpace

  return false; // unknown address, goes to LOADER
}

void PageFaultHandler::increaseUserStackSize(UserThread* thread)
{
  uint64 stack_start_addr = thread->getUserStackStartAddr();
  uint16 pages = thread->getStackPages();

  /*
  debug(PAGEFAULT, "[addrIsWithinAnyUserStack] stack start: %14zx\n", stack_start_addr);
  debug(PAGEFAULT, "[addrIsWithinAnyUserStack] stack end:   %14zx\n", stack_start_addr - STACK_MAX_SIZE);
  debug(PAGEFAULT, "[addrIsWithinAnyUserStack] given addr:  %14zx\n", address);
  debug(PAGEFAULT, "[addrIsWithinAnyUserStack] difference:  %14zx\n", stack_start_addr - address);
  */

  if (++pages > STACK_MAX_PAGES)
  {
    debug(PAGEFAULT, "[increaseUserStackSize] Maximum stack size reached!\n");
    Syscall::exit(-1);
    return;
  }

  uint64 vpn = (stack_start_addr - PAGE_SIZE * pages) / PAGE_SIZE;
  size_t ppn = PageManager::instance()->allocPPN();
  bool mapped = currentThread->t_loader_->arch_memory_.mapPage(vpn, ppn, true);

  if (!mapped)
  {
    debug(PAGEFAULT, "[increaseUserStackSize] Page could not be mapped!\n");
    PageManager::instance()->freePPN(ppn);
    Syscall::exit(-1);
    return;
  }

  thread->setStackPages(pages);
  debug(PAGEFAULT, "[increaseUserStackSize] Page mapped, user stack increased! (pages: %u)\n", pages);
}
