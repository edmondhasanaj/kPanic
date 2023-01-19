#pragma once

#include "types.h"
#include "paging-definitions.h"

/**
 * These are the basic offsets for our memory layout
 */

/**
 * this is the difference between link and load base
 */
#define PHYSICAL_TO_VIRTUAL_OFFSET 0xFFFFFFFF80000000ULL

/**
 * returns the physical address of a virtual address by using the offset
 */
#define VIRTUAL_TO_PHYSICAL_BOOT(x) ((void*)(~PHYSICAL_TO_VIRTUAL_OFFSET & ((uint64)x)))

/**
 * Use only the lower canonical half for userspace
 */
#define USER_BREAK 0x0000800000000000ULL

/**
 * start-addr of user stacks is higher than end-addr because they grow downwards
 */
#define ARGS_SEGMENT_START   ((USER_BREAK - 1) & ~(PAGE_SIZE - 1))    // 0x7fff_ffff_f000
#define ARGS_SEGMENT_END     (ARGS_SEGMENT_START - PAGE_SIZE)         // 0x7fff_ffff_e000

#define STACK_SPACE_START    (ARGS_SEGMENT_END - 1)                   // 0x7fff_ffff_dfff
#define STACK_SPACE_END      0x00002AAAAAAAAAAA                       // 0x2aaa_aaaa_aaaa

//#define STACK_MAX_SIZE      0x800000    //  8MiB = 2048 Pages each 4kiB // LINUX
#define STACK_MAX_SIZE       0xA000       // 40kiB =   10 Pages each 4kiB // SWEB
#define STACK_MAX_PAGES      10           // Since SWEB only supports somewhat about ~1008 pages, 10 pages should be enough for demonstrating that the stack grows.

/**   // Definitions: /arch/x86/64/include/offsets.h
 *;
 *;   ffff_ffff_ffff_ffff ─┐
 *;                        │ 128 TiB kernelSpace
 *;   ffff_8000_0000_0000 ─┘
 *;                       ±1
 *;   ffff_7fff_ffff_ffff ─┐
 *;                        │ ~16 EiB nonCanonicalSpace
 *;   0000_8000_0000_0000 ─┘ ← USER_BREAK
 *;                       ±1
 *;   0000_7fff_ffff_ffff    ← USER_SPACE_START
 *;                       ±(PAGE_SIZE-1)
 *;   0000_7fff_ffff_f000 ─┐ ← ARGS_SEGMENT_START
 *;                        │   1 PAGE_SIZE = 0x1000 = 4kiB
 *;   0000_7fff_ffff_efff ─┘ ← ARGS_SEGMENT_END
 *;                       ±1
 *;   0000_7fff_ffff_effe ─┐ ← STACK_SPACE_START
 *;                        │   ~85 TiB stackSpace (2/3)
 *;   0000_2AAA_AAAA_AAAA ─┘ ← STACK_SPACE_END
 *;                       ±1
 *;   0000_????_????_????    ← shared libraries (grows downward)
 *;   0000_????_????_????    ← shared memory (grows upward)
 *;   0000_????_????_????    ← heap (grows upward)
 *;   0000_????_????_????    ← bss
 *;   0000_????_????_????    ← data
 *;   0000_????_????_????    ← text
 *;   0000_0000_0000_0000    ← USER_SPACE_END
 */
