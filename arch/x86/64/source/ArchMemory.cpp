#include "COWManager.h"
#include "ArchMemory.h"
#include "ArchInterrupts.h"
#include "kprintf.h"
#include "assert.h"
#include "PageManager.h"
#include "kstring.h"
#include "ArchThreads.h"
#include "Thread.h"

PageMapLevel4Entry kernel_page_map_level_4[PAGE_MAP_LEVEL_4_ENTRIES] __attribute__((aligned(0x1000)));
PageDirPointerTableEntry kernel_page_directory_pointer_table[2 * PAGE_DIR_POINTER_TABLE_ENTRIES] __attribute__((aligned(0x1000)));
PageDirEntry kernel_page_directory[2 * PAGE_DIR_ENTRIES] __attribute__((aligned(0x1000)));
PageTableEntry kernel_page_table[8 * PAGE_TABLE_ENTRIES] __attribute__((aligned(0x1000)));


ArchMemory::ArchMemory() : pml4_lock_("pml4_lock_"), pdpt_lock_("pdpt_lock_"), pd_lock_("pd_lock_"),
                           pt_lock_("pt_lock_")
{
  page_map_level_4_ = PageManager::instance()->allocPPN();
  PageMapLevel4Entry* new_pml4 = (PageMapLevel4Entry*) getIdentAddressOfPPN(page_map_level_4_);
  memcpy((void*) new_pml4, (void*) kernel_page_map_level_4, PAGE_SIZE);
  memset(new_pml4, 0, PAGE_SIZE / 2); // should be zero, this is just for safety
}



template<typename T>
bool ArchMemory::checkAndRemove(pointer map_ptr, uint64 index)
{
  T* map = (T*) map_ptr;
  debug(A_MEMORY, "%s: page %p index %zx\n", __PRETTY_FUNCTION__, map, index);
  ((uint64*) map)[index] = 0;
  for (uint64 i = 0; i < PAGE_DIR_ENTRIES; i++)
  {
    if (map[i].present != 0)
      return false;
  }
  return true;
}

bool ArchMemory::unmapPage(uint64 virtual_page)
{
  ArchMemoryMapping m = resolveMapping(virtual_page);

  assert(m.page_ppn != 0 && m.page_size == PAGE_SIZE && m.pt[m.pti].present);

  pt_lock_.acquire();
  m.pt[m.pti].present = 0;
  pt_lock_.release();
  PageManager::instance()->freePPN(m.page_ppn);
  pt_lock_.acquire();
  ((uint64*)m.pt)[m.pti] = 0; // for easier debugging
  bool empty = checkAndRemove<PageTableEntry>(getIdentAddressOfPPN(m.pt_ppn), m.pti);
  pt_lock_.release();

  
  if (empty)
  {
    pd_lock_.acquire();
    empty = checkAndRemove<PageDirPageTableEntry>(getIdentAddressOfPPN(m.pd_ppn), m.pdi);
    pd_lock_.release();
    PageManager::instance()->freePPN(m.pt_ppn);
  }
  

  
  if (empty)
  {
    pdpt_lock_.acquire();
    empty = checkAndRemove<PageDirPointerTablePageDirEntry>(getIdentAddressOfPPN(m.pdpt_ppn), m.pdpti);
    pdpt_lock_.release();
    PageManager::instance()->freePPN(m.pd_ppn);
  }

  
  if (empty)
  {
    pml4_lock_.acquire();
    empty = checkAndRemove<PageMapLevel4Entry>(getIdentAddressOfPPN(m.pml4_ppn), m.pml4i);
    pml4_lock_.release();
    PageManager::instance()->freePPN(m.pdpt_ppn);
  }
  

  return true;
}

template<typename T>
bool ArchMemory::insert(pointer map_ptr, uint64 index, uint64 ppn, uint64 bzero, uint64 size, uint64 user_access,
                        uint64 writeable)
{
  assert(map_ptr & ~0xFFFFF00000000000ULL);
  T* map = (T*) map_ptr;
  debug(A_MEMORY, "%s: page %p index %zx ppn %zx user_access %zx size %zx\n", __PRETTY_FUNCTION__, map, index, ppn,
        user_access, size);
  if (bzero)
  {
    memset((void*) getIdentAddressOfPPN(ppn), 0, PAGE_SIZE);
    assert(((uint64* )map)[index] == 0);
  }
  map[index].size = size;
  map[index].writeable = writeable;
  map[index].page_ppn = ppn;
  map[index].user_access = user_access;
  map[index].present = 1;
  return true;
}

bool ArchMemory::mapPage(uint64 virtual_page, uint64 physical_page, uint64 user_access)
{
  debug(A_MEMORY, "%zx %zx %zx %zx\n", page_map_level_4_, virtual_page, physical_page, user_access);
  ArchMemoryMapping m = resolveMapping(page_map_level_4_, virtual_page);
  assert((m.page_size == 0) || (m.page_size == PAGE_SIZE));

  if (m.pdpt_ppn == 0)
  {
    m.pdpt_ppn = PageManager::instance()->allocPPN();
    pml4_lock_.acquire();
    insert<PageMapLevel4Entry>((pointer) m.pml4, m.pml4i, m.pdpt_ppn, 1, 0, 1, 1);
    pml4_lock_.release();
  }

  if (m.pd_ppn == 0)
  {
    m.pd_ppn = PageManager::instance()->allocPPN();
    pdpt_lock_.acquire();
    insert<PageDirPointerTablePageDirEntry>(getIdentAddressOfPPN(m.pdpt_ppn), m.pdpti, m.pd_ppn, 1, 0, 1, 1);
    pdpt_lock_.release();
  }
  

  if (m.pt_ppn == 0)
  {
    m.pt_ppn = PageManager::instance()->allocPPN();
    pd_lock_.acquire();
    insert<PageDirPageTableEntry>(getIdentAddressOfPPN(m.pd_ppn), m.pdi, m.pt_ppn, 1, 0, 1, 1);
    pd_lock_.release();
  }

  pt_lock_.acquire();
  if (m.page_ppn == 0)
  {
    bool insertion_valid = insert<PageTableEntry>(getIdentAddressOfPPN(m.pt_ppn), m.pti, physical_page, 0, 0, user_access, 1);
    pt_lock_.release();
    return insertion_valid;
  }
  pt_lock_.release();

  return false;
}

ArchMemory::~ArchMemory()
{
  assert(currentThread->kernel_registers_->cr3 != page_map_level_4_ * PAGE_SIZE && "thread deletes its own arch memory");

  PageMapLevel4Entry* pml4 = (PageMapLevel4Entry*) getIdentAddressOfPPN(page_map_level_4_);
  for (uint64 pml4i = 0; pml4i < PAGE_MAP_LEVEL_4_ENTRIES / 2; pml4i++) // free only lower half
  {
    if (pml4[pml4i].present)
    {
      PageDirPointerTableEntry* pdpt = (PageDirPointerTableEntry*) getIdentAddressOfPPN(pml4[pml4i].page_ppn);
      for (uint64 pdpti = 0; pdpti < PAGE_DIR_POINTER_TABLE_ENTRIES; pdpti++)
      {
        if (pdpt[pdpti].pd.present)
        {
          assert(pdpt[pdpti].pd.size == 0);
          PageDirEntry* pd = (PageDirEntry*) getIdentAddressOfPPN(pdpt[pdpti].pd.page_ppn);
          for (uint64 pdi = 0; pdi < PAGE_DIR_ENTRIES; pdi++)
          {
            if (pd[pdi].pt.present)
            {
              assert(pd[pdi].pt.size == 0);
              PageTableEntry* pt = (PageTableEntry*) getIdentAddressOfPPN(pd[pdi].pt.page_ppn);
              for (uint64 pti = 0; pti < PAGE_TABLE_ENTRIES; pti++)
              {
                if (pt[pti].present)
                {
                  pt[pti].present = 0;
                  PageManager::instance()->freePPN(pt[pti].page_ppn);
                }
              }
              pd[pdi].pt.present = 0;
              PageManager::instance()->freePPN(pd[pdi].pt.page_ppn);
            }
          }
          pdpt[pdpti].pd.present = 0;
          PageManager::instance()->freePPN(pdpt[pdpti].pd.page_ppn);
        }
      }
      pml4[pml4i].present = 0;
      PageManager::instance()->freePPN(pml4[pml4i].page_ppn);
    }
  }
  PageManager::instance()->freePPN(page_map_level_4_);
}

pointer ArchMemory::checkAddressValid(uint64 vaddress_to_check)
{
  ArchMemoryMapping m = resolveMapping(page_map_level_4_, vaddress_to_check / PAGE_SIZE);
  if (m.page != 0)
  {
    debug(A_MEMORY, "checkAddressValid %zx and %zx -> true\n", page_map_level_4_, vaddress_to_check);
    return m.page | (vaddress_to_check % m.page_size);
  }
  else
  {
    debug(A_MEMORY, "checkAddressValid %zx and %zx -> false\n", page_map_level_4_, vaddress_to_check);
    return 0;
  }
}

ArchMemoryMapping ArchMemory::resolveMapping(uint64 vpage)
{
  return resolveMapping(page_map_level_4_, vpage);
}

ArchMemoryMapping ArchMemory::resolveMapping(uint64 pml4, uint64 vpage)
{
  ArchMemoryMapping m;

  m.pti = vpage;
  m.pdi = m.pti / PAGE_TABLE_ENTRIES;
  m.pdpti = m.pdi / PAGE_DIR_ENTRIES;
  m.pml4i = m.pdpti / PAGE_DIR_POINTER_TABLE_ENTRIES;

  m.pti %= PAGE_TABLE_ENTRIES;
  m.pdi %= PAGE_DIR_ENTRIES;
  m.pdpti %= PAGE_DIR_POINTER_TABLE_ENTRIES;
  m.pml4i %= PAGE_MAP_LEVEL_4_ENTRIES;

  assert(pml4 < PageManager::instance()->getTotalNumPages());
  m.pml4 = (PageMapLevel4Entry*) getIdentAddressOfPPN(pml4);
  m.pdpt = 0;
  m.pd = 0;
  m.pt = 0;
  m.page = 0;
  m.pml4_ppn = pml4;
  m.pdpt_ppn = 0;
  m.pd_ppn = 0;
  m.pt_ppn = 0;
  m.page_ppn = 0;
  m.page_size = 0;

  if (m.pml4[m.pml4i].present)
  {
    m.pdpt_ppn = m.pml4[m.pml4i].page_ppn;
    m.pdpt = (PageDirPointerTableEntry*) getIdentAddressOfPPN(m.pml4[m.pml4i].page_ppn);
    if (m.pdpt[m.pdpti].pd.present && !m.pdpt[m.pdpti].pd.size) // 1gb page ?
    {
      m.pd_ppn = m.pdpt[m.pdpti].pd.page_ppn;
      if (m.pd_ppn > PageManager::instance()->getTotalNumPages())
      {
        debug(A_MEMORY, "%zx\n", m.pd_ppn);
      }
      assert(m.pd_ppn < PageManager::instance()->getTotalNumPages());
      m.pd = (PageDirEntry*) getIdentAddressOfPPN(m.pdpt[m.pdpti].pd.page_ppn);
      if (m.pd[m.pdi].pt.present && !m.pd[m.pdi].pt.size) // 2mb page ?
      {
        m.pt_ppn = m.pd[m.pdi].pt.page_ppn;
        assert(m.pt_ppn < PageManager::instance()->getTotalNumPages());
        m.pt = (PageTableEntry*) getIdentAddressOfPPN(m.pd[m.pdi].pt.page_ppn);
        if (m.pt[m.pti].present)
        {
          m.page = getIdentAddressOfPPN(m.pt[m.pti].page_ppn);
          m.page_ppn = m.pt[m.pti].page_ppn;
          assert(m.page_ppn < PageManager::instance()->getTotalNumPages());
          m.page_size = PAGE_SIZE;
        }
      }
      else if (m.pd[m.pdi].page.present)
      {
        m.page_size = PAGE_SIZE * PAGE_TABLE_ENTRIES;
        m.page_ppn = m.pd[m.pdi].page.page_ppn;
        m.page = getIdentAddressOfPPN(m.pd[m.pdi].page.page_ppn);
      }
    }
    else if (m.pdpt[m.pdpti].page.present)
    {
      m.page_size = PAGE_SIZE * PAGE_TABLE_ENTRIES * PAGE_DIR_ENTRIES;
      m.page_ppn = m.pdpt[m.pdpti].page.page_ppn;
      assert(m.page_ppn < PageManager::instance()->getTotalNumPages());
      m.page = getIdentAddressOfPPN(m.pdpt[m.pdpti].page.page_ppn);
    }
  }
  return m;
}

size_t ArchMemory::get_PPN_Of_VPN_In_KernelMapping(size_t virtual_page, size_t *physical_page,
                                                   size_t *physical_pte_page)
{
  ArchMemoryMapping m = resolveMapping(((uint64) VIRTUAL_TO_PHYSICAL_BOOT(kernel_page_map_level_4) / PAGE_SIZE),
                                       virtual_page);
  if (physical_page)
    *physical_page = m.page_ppn;
  if (physical_pte_page)
    *physical_pte_page = m.pt_ppn;
  return m.page_size;
}

void ArchMemory::mapKernelPage(size_t virtual_page, size_t physical_page)
{
  ArchMemoryMapping mapping = resolveMapping(((uint64) VIRTUAL_TO_PHYSICAL_BOOT(kernel_page_map_level_4) / PAGE_SIZE),
                                             virtual_page);
  PageMapLevel4Entry* pml4 = kernel_page_map_level_4;
  assert(pml4[mapping.pml4i].present);
  PageDirPointerTableEntry *pdpt = (PageDirPointerTableEntry*) getIdentAddressOfPPN(pml4[mapping.pml4i].page_ppn);
  assert(pdpt[mapping.pdpti].pd.present);
  PageDirEntry *pd = (PageDirEntry*) getIdentAddressOfPPN(pdpt[mapping.pdpti].pd.page_ppn);
  assert(pd[mapping.pdi].pt.present);
  PageTableEntry *pt = (PageTableEntry*) getIdentAddressOfPPN(pd[mapping.pdi].pt.page_ppn);
  assert(!pt[mapping.pti].present);
  pt[mapping.pti].writeable = 1;
  pt[mapping.pti].page_ppn = physical_page;
  pt[mapping.pti].present = 1;
  asm volatile ("movq %%cr3, %%rax; movq %%rax, %%cr3;" ::: "%rax");
}

void ArchMemory::unmapKernelPage(size_t virtual_page)
{
  ArchMemoryMapping mapping = resolveMapping(((uint64) VIRTUAL_TO_PHYSICAL_BOOT(kernel_page_map_level_4) / PAGE_SIZE),
                                             virtual_page);
  PageMapLevel4Entry* pml4 = kernel_page_map_level_4;
  assert(pml4[mapping.pml4i].present);
  PageDirPointerTableEntry *pdpt = (PageDirPointerTableEntry*) getIdentAddressOfPPN(pml4[mapping.pml4i].page_ppn);
  assert(pdpt[mapping.pdpti].pd.present);
  PageDirEntry *pd = (PageDirEntry*) getIdentAddressOfPPN(pdpt[mapping.pdpti].pd.page_ppn);
  assert(pd[mapping.pdi].pt.present);
  PageTableEntry *pt = (PageTableEntry*) getIdentAddressOfPPN(pd[mapping.pdi].pt.page_ppn);
  assert(pt[mapping.pti].present);
  pt[mapping.pti].present = 0;
  pt[mapping.pti].writeable = 0;
  PageManager::instance()->freePPN(pt[mapping.pti].page_ppn);
  asm volatile ("movq %%cr3, %%rax; movq %%rax, %%cr3;" ::: "%rax");
}

uint64 ArchMemory::getRootOfPagingStructure()
{
  return page_map_level_4_;
}

PageMapLevel4Entry* ArchMemory::getRootOfKernelPagingStructure()
{
  return kernel_page_map_level_4;
}

void ArchMemory::copyPagesToNewArchMem(ArchMemory& new_mem)
{
  //assert(currentThread->kernel_registers_->cr3 != page_map_level_4_ * PAGE_SIZE && "thread deletes its own arch memory");

  auto pml4 = (PageMapLevel4Entry*) getIdentAddressOfPPN(page_map_level_4_);

  for (uint64 pml4i = 0; pml4i < PAGE_MAP_LEVEL_4_ENTRIES / 2; pml4i++) // iterate over the lower half
  {
    if (pml4[pml4i].present)
    {
      auto pdpt = (PageDirPointerTableEntry*) getIdentAddressOfPPN(pml4[pml4i].page_ppn);

      for (uint64 pdpti = 0; pdpti < PAGE_DIR_POINTER_TABLE_ENTRIES; pdpti++)
      {
        if (pdpt[pdpti].pd.present)
        {
          assert(pdpt[pdpti].pd.size == 0);
          auto pd = (PageDirEntry*) getIdentAddressOfPPN(pdpt[pdpti].pd.page_ppn);

          for (uint64 pdi = 0; pdi < PAGE_DIR_ENTRIES; pdi++)
          {
            if (pd[pdi].pt.present)
            {
              assert(pd[pdi].pt.size == 0);
              auto pt = (PageTableEntry*) getIdentAddressOfPPN(pd[pdi].pt.page_ppn);

              for (uint64 pti = 0; pti < PAGE_TABLE_ENTRIES; pti++)
              {
                if (pt[pti].present)
                {
                  size_t phys_page = PageManager::instance()->allocPPN();
                  uint64 virt_addr = ((pml4i) << (9 + 9 + 9 + 12)) +
                                     ((pdpti) << (9 + 9 + 12)) +
                                     ((pdi)   << (9 + 12)) +
                                     ((pti)   << (12));

                  // TODO: to enable cow we need to pass the old page_ppn into this function
                  bool vpn_mapped = copyPageHierarchy(virt_addr / PAGE_SIZE, phys_page,
                                                      new_mem, 1);
                  assert(vpn_mapped && "Virtual page was already mapped - this should never happen");

                  // copy page contents
                  pointer old_ident = getIdentAddressOfPPN(pt[pti].page_ppn);
                  pointer new_ident = getIdentAddressOfPPN(phys_page);
                  memcpy((void*)new_ident, (void*)old_ident, PAGE_SIZE);
                }
              }
            }
          }
        }
      }
    }
  }
}

void ArchMemory::copyArgsSegmentToNewArchMem(ArchMemory& new_mem, size_t old_args_ppn)
{
  size_t new_args_ppn = PageManager::instance()->allocPPN();
  uint64 virt_addr = ARGS_SEGMENT_START;

  // mapPage()
  bool vpn_mapped = new_mem.mapPage(virt_addr / PAGE_SIZE - 1, new_args_ppn, 1);
  //bool vpn_mapped = new_mem.copyPageHierarchy(virt_addr / PAGE_SIZE - 1, new_args_ppn, new_mem);
  assert(vpn_mapped && "Virtual page was already mapped - this should never happen");

  // copy page contents
  pointer old_ident = getIdentAddressOfPPN(old_args_ppn);
  pointer new_ident = getIdentAddressOfPPN(new_args_ppn);
  memcpy((void*)new_ident, (void*)old_ident, PAGE_SIZE);
}

template<typename T>
bool ArchMemory::insertCopy(pointer map_ptr, uint64 index, uint64 ppn, uint64 writeable, T old)
{
  assert(map_ptr & ~0xFFFFF00000000000ULL);
  T* map = (T*) map_ptr;

  if(writeable) {
    memset((void *) getIdentAddressOfPPN(ppn), 0, PAGE_SIZE);
    assert(((uint64 *) map)[index] == 0);
  }

  map[index] = old;
  map[index].page_ppn = ppn;
  map[index].writeable = writeable;

  return true;
}

bool ArchMemory::copyPageHierarchy(uint64 virtual_page, uint64 physical_page, ArchMemory& new_mem, uint64 writeable) {

  ArchMemoryMapping new_mapping = new_mem.resolveMapping(virtual_page);
  ArchMemoryMapping old_mapping = this->resolveMapping(virtual_page);

  assert((new_mapping.page_size == 0) || (new_mapping.page_size == PAGE_SIZE));

  if (new_mapping.pdpt_ppn == 0)
  {
    new_mapping.pdpt_ppn = PageManager::instance()->allocPPN();
    insertCopy<PageMapLevel4Entry>((pointer) new_mapping.pml4, new_mapping.pml4i, new_mapping.pdpt_ppn, 1,
                                   old_mapping.pml4[old_mapping.pml4i]);
  }

  if (new_mapping.pd_ppn == 0)
  {
    new_mapping.pd_ppn = PageManager::instance()->allocPPN();
    insertCopy<PageDirPointerTablePageDirEntry>(getIdentAddressOfPPN(new_mapping.pdpt_ppn), new_mapping.pdpti,
                                                new_mapping.pd_ppn, 1, old_mapping.pdpt[old_mapping.pdpti].pd);
  }

  if (new_mapping.pt_ppn == 0)
  {
    new_mapping.pt_ppn = PageManager::instance()->allocPPN();
    insertCopy<PageDirPageTableEntry>(getIdentAddressOfPPN(new_mapping.pd_ppn), new_mapping.pdi, new_mapping.pt_ppn, 1,
                                      old_mapping.pd[old_mapping.pdi].pt);
  }

  if (new_mapping.page_ppn == 0)
  {
    return insertCopy<PageTableEntry>(getIdentAddressOfPPN(new_mapping.pt_ppn), new_mapping.pti, physical_page, writeable,
                                      old_mapping.pt[old_mapping.pti]);
  }

  return false;
}

void ArchMemory::copyPagesToNewArchMemCOW(ArchMemory &new_mem, size_t pid1, size_t pid2) {
  auto pml4 = (PageMapLevel4Entry*) getIdentAddressOfPPN(page_map_level_4_);

  for (uint64 pml4i = 0; pml4i < PAGE_MAP_LEVEL_4_ENTRIES / 2; pml4i++) // iterate over the lower half
  {
    if (pml4[pml4i].present)
    {
      auto pdpt = (PageDirPointerTableEntry*) getIdentAddressOfPPN(pml4[pml4i].page_ppn);

      for (uint64 pdpti = 0; pdpti < PAGE_DIR_POINTER_TABLE_ENTRIES; pdpti++)
      {
        if (pdpt[pdpti].pd.present)
        {
          assert(pdpt[pdpti].pd.size == 0);
          auto pd = (PageDirEntry*) getIdentAddressOfPPN(pdpt[pdpti].pd.page_ppn);

          for (uint64 pdi = 0; pdi < PAGE_DIR_ENTRIES; pdi++)
          {
            if (pd[pdi].pt.present)
            {
              assert(pd[pdi].pt.size == 0);
              auto pt = (PageTableEntry*) getIdentAddressOfPPN(pd[pdi].pt.page_ppn);

              for (uint64 pti = 0; pti < PAGE_TABLE_ENTRIES; pti++)
              {
                if (pt[pti].present)
                {
                  uint64 virt_addr = ((pml4i) << (9 + 9 + 9 + 12)) +
                                     ((pdpti) << (9 + 9 + 12)) +
                                     ((pdi)   << (9 + 12)) +
                                     ((pti)   << (12));

                  // the same ppn as in the old archmem is passed to the function to enable cow
                  bool vpn_mapped = copyPageHierarchy(virt_addr / PAGE_SIZE, pt[pti].page_ppn,
                                                      new_mem, 0);
                  assert(vpn_mapped && "Virtual page was already mapped - this should never happen");

                  // set the old processes writeable bit to 0
                  pt[pti].writeable = 0;

                  COWManager::instance()->addToCOWMap(pt[pti].page_ppn, pid1, pid2);
                  debug(COWMANAGER, "pid: %lu and pid: %lu share the ppn: %ld (virt_addr: %p)\n", pid1, pid2, pt[pti].page_ppn, (void*)virt_addr);

                }
              }
            }
          }
        }
      }
    }
  }
}





