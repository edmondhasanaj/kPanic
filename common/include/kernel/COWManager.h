#pragma once

#include "umap.h"
#include "uset.h"
#include "UserProcess.h"

class COWManager {
  public:
    /**
     * COWManager is a Singleton and this function returns the instance
     * @return instance of COWManager
     */
    static COWManager* instance();

    void addToCOWMap(uint32 ppn, size_t pid1, size_t pid2);

    /**
     * checks if the ppn exists in cow_map_ and also checks whether the set of
     * UserProcesses of that ppn is empty
     * @param ppn the desired ppn
     * @return true  -> if the ppn is in the map and the set is NOT empty
     *         false -> if the ppn is not in the map or the set is empty
     */
    bool ppnShared(uint32 ppn);
    bool ppnShared(pointer virt_address);

    /**
     * when deleting the resources of a process, all references in the
     * COWManager also need to be deleted
     * @param proc the process to erase from the map
     */
    void eraseProcessFromCOWMap(UserProcess* proc);

    /**
     * main function for COWManger. The mapping of virt_address is resolved
     * to get it's ppn, then we look at how many processes share the ppn and handle
     * the page fault accordingly
     * @param virt_address
     */
    void handleCOWPageFault(pointer virt_address);

    /**
     * deletes a process from a single page if the page is shared
     * @param proc process to delete
     * @param ppn the ppn to delete the process from
     */
    void eraseProcessFromPage(UserProcess* proc, uint32 ppn);

  private: /// private methods are NOT LOCKED!
    COWManager();

    static COWManager* instance_;

    /**
     * this map stores the information about which processes share which ppn
     * if a ppn is once in this map, it stays there. The set can be checked for .empty()
     * to see if the ppn is shared (see ppnShared())
     * size_t                  -> the shared ppn
     * ustl::set<UserProcess*> -> the list of processes sharing that ppn
     *                            set<> guarantees that no duplicates get inserted
     */
    ustl::map<uint32, ustl::set<UserProcess*>> cow_map_;
    Mutex cow_map_lock_;
};