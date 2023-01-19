#include <ProcessRegistry.h>
#include <PageManager.h>
#include "COWManager.h"
#include "types.h"

COWManager* COWManager::instance_ = nullptr;

COWManager::COWManager() : cow_map_lock_("cow_list_lock")
{
  debug(COWMANAGER, "cow manager constructed!\n");
}

COWManager* COWManager::instance()
{
  if(unlikely(!instance_)) {
    instance_ = new COWManager();
  }

  return instance_;
}

void COWManager::addToCOWMap(uint32 ppn, size_t pid1, size_t pid2) {
  auto proc1 = ProcessRegistry::instance()->getUserProcessFromPid(pid1);
  auto proc2 = ProcessRegistry::instance()->getUserProcessFromPid(pid2);

  assert(proc1 != nullptr && proc2 != nullptr && "processes for copy on write are not in the process list");

  cow_map_lock_.acquire();
  // inserts a new ppn if not there yet, otherwise appends to the existing ppn
  cow_map_[ppn].insert(proc1);
  cow_map_[ppn].insert(proc2);
  cow_map_lock_.release();
}

bool COWManager::ppnShared(uint32 ppn) {
  cow_map_lock_.acquire();
  if(cow_map_.find(ppn) == cow_map_.end()) {
    cow_map_lock_.release();
    return false;
  }

  if(cow_map_[ppn].empty()) {
    cow_map_lock_.release();
    return false;
  }
  cow_map_lock_.release();

  return true;
}

bool COWManager::ppnShared(pointer virt_address) {
  auto thread = (UserThread*)currentThread;
  auto mapping = thread->getParentProc()->getLoader()->arch_memory_.resolveMapping(virt_address / PAGE_SIZE);

  return ppnShared((uint32)mapping.page_ppn);
}

void COWManager::eraseProcessFromCOWMap(UserProcess *proc) {
  debug(COWMANAGER, "erasing %s from cow_map_\n", proc->getFilename().c_str());
  cow_map_lock_.acquire();
  for (const auto& pair : cow_map_) {
    auto set = pair.second;

    if(set.find(proc) != set.end()) {
      cow_map_[pair.first].erase(proc);
    }
  }
  cow_map_lock_.release();
}

void COWManager::handleCOWPageFault(pointer virt_address) {
  debug(COWMANAGER, "handling cow pagefault\n");

  auto thread  = (UserThread*)currentThread;
  auto proc    = thread->getParentProc();
  auto mapping = proc->getLoader()->arch_memory_.resolveMapping(virt_address / PAGE_SIZE);

  cow_map_lock_.acquire();
  assert(cow_map_[mapping.page_ppn].find(proc) != cow_map_[mapping.page_ppn].end() &&
         "The current process is not in the set of processes that share the ppn");

  size_t size_of_set = cow_map_[mapping.page_ppn].size();

  if(size_of_set == 0) {
    assert(false && "number of processes sharing a page is 0 and a cow-pagefault happend on that page");
  }

  // only 1 process -> - set the writeable bit
  //                   - erase the proc from the ppn in cow_map_
  else if(size_of_set == 1) {
    debug(COWMANAGER, "[COW] case 1: only 1 process left in the set\n");
    mapping.pt[mapping.pti].writeable = 1;
    cow_map_[mapping.page_ppn].erase(proc);
  }

  // 2 processes -> - allocate a new ppn
  //                - set the writeable bit of the earlier resolved mapping
  //                - copy the memory content to the new ppn via the ident-address
  //                - erase the process for which we already handled the pagefault from
  //                  cow_map_
  //                - get the other process that is still in the set for the wanted ppn
  //                  in cow_map_
  //                - resolve the mapping in the other process
  //                - set the writeable bit to 1
  //                - erase the other process from cow_map_
  else if(size_of_set == 2) {
    debug(COWMANAGER, "[COW] case 2: 2 process left in the set\n");

    // allocate a new ppn and set the pagetableentry to it
    size_t new_ppn = PageManager::instance()->allocPPN();
    mapping.pt[mapping.pti].page_ppn  = new_ppn;
    mapping.pt[mapping.pti].writeable = 1;

    // copy the content to the new page
    pointer old_ident = proc->getLoader()->arch_memory_.getIdentAddressOfPPN(mapping.page_ppn);
    pointer new_ident = proc->getLoader()->arch_memory_.getIdentAddressOfPPN(new_ppn);
    memcpy((void*)new_ident, (void*)old_ident, PAGE_SIZE);

    cow_map_[mapping.page_ppn].erase(proc);

    // the other process keeps the page
    auto other_proc = cow_map_[mapping.page_ppn].at(0);

    auto other_mapping = other_proc->getLoader()->arch_memory_.resolveMapping(virt_address / PAGE_SIZE);
    other_mapping.pt[other_mapping.pti].writeable = 1;

    cow_map_[mapping.page_ppn].erase(other_proc);
  }

  // 3 or more processes -> - allocate a new ppn
  //                        - copy the memory content to the new ppn via the
  //                          ident-address
  //                        - set the new ppn in the mapping of the process
  //                        - set the writable bit
  //                        - erase the process from cow_map_
  else if(size_of_set >= 3) {
    debug(COWMANAGER, "[COW] case 3: %lu process left in the set\n", size_of_set);

    size_t ppn = PageManager::instance()->allocPPN();

    pointer old_ident = proc->getLoader()->arch_memory_.getIdentAddressOfPPN(mapping.page_ppn);
    pointer new_ident = proc->getLoader()->arch_memory_.getIdentAddressOfPPN(ppn);
    memcpy((void*)new_ident, (void*)old_ident, PAGE_SIZE);

    mapping.pt[mapping.pti].page_ppn  = ppn;
    mapping.pt[mapping.pti].writeable = 1;

    cow_map_[mapping.page_ppn].erase(proc);
  }

  cow_map_lock_.release();
}

void COWManager::eraseProcessFromPage(UserProcess* proc, uint32 ppn) {
  if(ppnShared(ppn)) {
    cow_map_lock_.acquire();
    cow_map_[ppn].erase(proc);
    cow_map_lock_.release();
  }
}


