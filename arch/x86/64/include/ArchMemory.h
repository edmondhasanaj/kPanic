#pragma once

#include "types.h"
#include "offsets.h"
#include "paging-definitions.h"
#include "Mutex.h"

class ArchMemoryMapping
{
  public:
    PageMapLevel4Entry* pml4;
    PageDirPointerTableEntry* pdpt;
    PageDirEntry* pd;
    PageTableEntry* pt;
    pointer page;

    uint64 pml4_ppn;
    uint64 pdpt_ppn;
    uint64 pd_ppn;
    uint64 pt_ppn;
    uint64 page_ppn;

    uint64 page_size;

    uint64 pml4i;
    uint64 pdpti;
    uint64 pdi;
    uint64 pti;
};

class ArchMemory
{
public:
    ArchMemory();

/**
 *
 * maps a virtual page to a physical page (pde and pte need to be set up first)
 *
 * @param physical_page_directory_page Real Page where the PDE to work on resides
 * @param virtual_page 
 * @param physical_page
 * @param user_access PTE User/Supervisor Flag, governing the binary Paging
 * Privilege Mechanism
 */
  __attribute__((warn_unused_result)) bool mapPage(uint64 virtual_page, uint64 physical_page, uint64 user_access);

/**
 * removes the mapping to a virtual_page by marking its PTE Entry as non valid
 *
 * @param physical_page_directory_page Real Page where the PDE to work on resides
 * @param virtual_page which will be invalidated
 */
  bool unmapPage(uint64 virtual_page);

  ~ArchMemory();

/**
 * Takes a Physical Page Number in Real Memory and returns a virtual address than
 * can be used to access given page
 * @param ppn Physical Page Number
 * @param page_size Optional, defaults to 4k pages, but you ned to set it to
 * 1024*4096 if you have a 4m page number
 * @return Virtual Address above 3GB pointing to the start of a memory segment that
 * is mapped to the physical page given
 */
    static pointer getIdentAddressOfPPN(uint64 ppn, uint32 page_size=PAGE_SIZE)
    {
      return 0xFFFFF00000000000ULL | (ppn * page_size);
    }

/**
 * Checks if a given Virtual Address is valid and is mapped to real memory
 * @param vaddress_to_check Virtual Address we want to check
 * @return true: if mapping exists\nfalse: if the given virtual address is unmapped
 * and accessing it would result in a pageFault
 */
  pointer checkAddressValid(uint64 vaddress_to_check);

/**
 * Takes a virtual_page and search through the pageTable and pageDirectory for the
 * physical_page it refers to
 * to get a physical address (which you can only use by adding 3gb to it) multiply
 * the &physical_page with the return value
 * @param virtual_page virtual Page to look up
 * @param *physical_page Pointer to the result
 * @param *physical_pte_page optional: Pointer to physical page number of used PTE, or unchanged if 4MiB Page
 * @return 0: if the virtual page doesn't map to any physical page\notherwise
 * returns the page size in byte (4096 for 4KiB pages or 4096*1024 for 4MiB pages)
 */
  static uint64 get_PPN_Of_VPN_In_KernelMapping(uint64 virtual_page, uint64 *physical_page, uint64 *physical_pte_page=0);
  ArchMemoryMapping resolveMapping(uint64 vpage);
  static ArchMemoryMapping resolveMapping(uint64 pml4,uint64 vpage);

/**
 *
 * maps a virtual page to a physical page in kernel mapping
 *
 * @param virtual_page
 * @param physical_page
 */
  static void mapKernelPage(uint64 virtual_page, uint64 physical_page);

/**
 * removes the mapping to a virtual_page by marking its PTE Entry as non valid
 * in kernel mapping
 *
 * @param virtual_page which will be invalidated
 */
  static void unmapKernelPage(uint64 virtual_page);

  uint64 page_map_level_4_;

  uint64 getRootOfPagingStructure();
  static PageMapLevel4Entry* getRootOfKernelPagingStructure();

  static const size_t RESERVED_START = 0xFFFFFFFF80000ULL;
  static const size_t RESERVED_END = 0xFFFFFFFFC0000ULL;


  /**
   * iterates over all the pages of an ArchMemory Instance and copies the mapped pages to another
   * ArchMemory Instance (used in the copy Constructor of UserProcess for fork())
   * @param new_mem the instance to copy to
   */
  void copyPagesToNewArchMem(ArchMemory& new_mem);

  /**
   * iterates over all the pages of an ArchMemory Instance and maps the ppn's to another
   * ArchMemory Instance (used in the copy Constructor of UserProcess for fork())
   * @param new_mem the instance to map to
   */
  void copyPagesToNewArchMemCOW(ArchMemory& new_mem, size_t pid1, size_t pid2);
  void copyArgsSegmentToNewArchMem(ArchMemory& new_mem, size_t args_ppn);

private:
  /**
   * locks for all paging levels
   */
  Mutex pml4_lock_;
  Mutex pdpt_lock_;
  Mutex pd_lock_;
  Mutex pt_lock_;

  /**
   * Almost the same as insert<T>()
   * Adds a page directory entry to the given page directory.
   * The difference is, that we use this function for 1:1 copying the bits of each page hierarchy entry
   * (the ppn is not copied, but everything else is)
   * Used while forking
   * @tparam T template for the hierarchy (is used from PML4 down to PT)
   * @param map_ptr pointer to the start of the page
   * @param index
   * @param ppn the ppn to set
   * @param old the old entry to copy from
   * @return always returns true if no assertion fails
   */
  template <typename T> static bool insertCopy(pointer map_ptr, uint64 index, uint64 ppn, uint64 writeable, T old);

  /**
   * used to set up the paging hierarchy for a new ArchMemory Instance when forking. We don't
   * use mapPage(), because we want to copy the exact bits of each entry which mapPage doesn't do.
   * @param virtual_page page to map
   * @param physical_page page to point the page table to
   * @param new_mem the new ArchMemory Instance
   * @return true  -> page hierarchy was set up correctly
   *         false -> page was already mapped (which should never happen)
   */
  bool copyPageHierarchy(uint64 virtual_page, uint64 physical_page, ArchMemory& new_mem, uint64 writeable);


  /**
   * Adds a page directory entry to the given page directory.
   * (In other words, adds the reference to a new page table to a given
   * page directory.)
   *
   * @param physical_page_directory_page physical page containing the target PD.
   * @param pde_vpn Index of the PDE (i.e. the page table) in the PD.
   * @param physical_page_table_page physical page of the new page table.
   */
  template <typename T> static bool insert(pointer map_ptr, uint64 index, uint64 ppn, uint64 bzero, uint64 size, uint64 user_access, uint64 writeable);

  /**
   * Removes a page directory entry from a given page directory if it is present
   * in the first place. Futhermore, the target page table is assured to be
   * empty.
   *
   * @param physical_page_directory_page physical page containing the target PD.
   * @param pde_vpn Index of the PDE (i.e. the page table) in the PD.
   */
  template<typename T> static bool checkAndRemove(pointer map_ptr, uint64 index);

  ArchMemory(ArchMemory const &src);
  ArchMemory &operator=(ArchMemory const &src);

};

