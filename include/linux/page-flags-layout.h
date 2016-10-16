#ifndef PAGE_FLAGS_LAYOUT_H
#define PAGE_FLAGS_LAYOUT_H

#include <linux/numa.h>
#include <generated/bounds.h>

/*
 * When a memory allocation must conform to specific limitations (such
 * as being suitable for DMA) the caller will pass in hints to the
 * allocator in the gfp_mask, in the zone modifier bits.  These bits
 * are used to select a priority ordered list of memory zones which
 * match the requested limits. See gfp_zone() in include/linux/gfp.h
 */

/* IAMROOT-12AB:
 * -------------
 * zone을 표현하는 비트는 zone이 1=0, 2=1bit, 3~4=2bit, 5이상은 에러
 */
/* IAMROOT-12 fehead (2016-10-16):
 * --------------------------
 * 라즈베리파이2
 *  MAX_NR_ZONES = 2
 *  ZONES_SHIFT = 1
 */
#if MAX_NR_ZONES < 2
#define ZONES_SHIFT 0
#elif MAX_NR_ZONES <= 2
#define ZONES_SHIFT 1
#elif MAX_NR_ZONES <= 4
#define ZONES_SHIFT 2
#else
#error ZONES_SHIFT -- too many zones configured adjust calculation
#endif

#ifdef CONFIG_SPARSEMEM
#include <asm/sparsemem.h>


/* IAMROOT-12AB:
 * -------------
 * SECTION_SHIFT: 물리메모리를 섹션단위로 나눌 때 필요 관리 비트 수
 * 예) MAX_PHYSMEM_BITS=32 (4GB), SECTION_SIZE_BITS=28 (256MB)
 *     -> SECTION_SHIFT=4 (2^4=16개의 섹션)
 */
/* SECTION_SHIFT	#bits space required to store a section # */
#define SECTIONS_SHIFT	(MAX_PHYSMEM_BITS - SECTION_SIZE_BITS)

#endif /* CONFIG_SPARSEMEM */

/*
 * page->flags layout:
 *
 * There are five possibilities for how page->flags get laid out.  The first
 * pair is for the normal case without sparsemem. The second pair is for
 * sparsemem when there is plenty of space for node and section information.
 * The last is when there is insufficient space in page->flags and a separate
 * lookup is necessary.
 *
 * No sparsemem or sparsemem vmemmap: |       NODE     | ZONE |             ... | FLAGS |
 *      " plus space for last_cpupid: |       NODE     | ZONE | LAST_CPUPID ... | FLAGS |
 * classic sparse with space for node:| SECTION | NODE | ZONE |             ... | FLAGS |
 *      " plus space for last_cpupid: | SECTION | NODE | ZONE | LAST_CPUPID ... | FLAGS |
 * classic sparse no space for node:  | SECTION |     ZONE    | ... | FLAGS |
 */

/* IAMROOT-12AB:
 * -------------
 * 섹션을 포현하는데 사용하는 비트 수
 */
/* IAMROOT-12 fehead (2016-10-16):
 * --------------------------
 * SECTIONS_WIDTH = 0
 */
#if defined(CONFIG_SPARSEMEM) && !defined(CONFIG_SPARSEMEM_VMEMMAP)
#define SECTIONS_WIDTH		SECTIONS_SHIFT
#else
#define SECTIONS_WIDTH		0
#endif

/* IAMROOT-12AB:
 * -------------
 * zone을 표현하는 비트는 zone이 1=0, 2=1bit, 3~4=2bit, 5이상은 에러
 * ZONES_WIDTH = ZONES_SHIFT = 1
 */
#define ZONES_WIDTH		ZONES_SHIFT

/* IAMROOT-12AB:
 * -------------
 * 노드를 표현하는 비트 수
 */
/* IAMROOT-12 fehead (2016-10-16):
 * --------------------------
 * SECTIONS_WIDTH = 0, ZONES_WIDTH = 1, NODES_SHIFT = 0
 * BITS_PER_LONG - NR_PAGEFLAGS = 32 - 22
 *
 * NODES_WIDTH =  NODES_SHIFT = 0
 */
#if SECTIONS_WIDTH+ZONES_WIDTH+NODES_SHIFT <= BITS_PER_LONG - NR_PAGEFLAGS
#define NODES_WIDTH		NODES_SHIFT
#else
#ifdef CONFIG_SPARSEMEM_VMEMMAP
#error "Vmemmap: No space for nodes field in page flags"
#endif
#define NODES_WIDTH		0
#endif

#ifdef CONFIG_NUMA_BALANCING
#define LAST__PID_SHIFT 8
#define LAST__PID_MASK  ((1 << LAST__PID_SHIFT)-1)

#define LAST__CPU_SHIFT NR_CPUS_BITS
#define LAST__CPU_MASK  ((1 << LAST__CPU_SHIFT)-1)


/* IAMROOT-12AB:
 * -------------
 * 8(PID) + n(CPU)를 표현하는데 사용하는 비트 수
 */
#define LAST_CPUPID_SHIFT (LAST__PID_SHIFT+LAST__CPU_SHIFT)
#else
/* IAMROOT-12 fehead (2016-10-16):
 * --------------------------
 * 라즈베리파이2
 */
#define LAST_CPUPID_SHIFT 0
#endif

/* IAMROOT-12 fehead (2016-10-16):
 * --------------------------
 * SECTIONS_WIDTH = 0, ZONES_WIDTH = 1, NODES_SHIFT = 0, LAST_CPUPID_SHIFT = 0
 * BITS_PER_LONG = 32, NR_PAGEFLAGS	22(0x16)
 * 
 * LAST_CPUPID_WIDTH = LAST_CPUPID_SHIFT = 0
 */
#if SECTIONS_WIDTH+ZONES_WIDTH+NODES_SHIFT+LAST_CPUPID_SHIFT <= BITS_PER_LONG - NR_PAGEFLAGS
#define LAST_CPUPID_WIDTH LAST_CPUPID_SHIFT
#else
#define LAST_CPUPID_WIDTH 0
#endif

/*
 * We are going to use the flags for the page to node mapping if its in
 * there.  This includes the case where there is no node, so it is implicit.
 */
/* IAMROOT-12 fehead (2016-10-16):
 * --------------------------
 * 라즈베리파이는 NODES_WIDTH = 1 , NODES_SHIFT 이므로
 * NODE_NOT_IN_PAGE_FLAGS 정의가 없음.
 */
#if !(NODES_WIDTH > 0 || NODES_SHIFT == 0)
#define NODE_NOT_IN_PAGE_FLAGS
#endif

#if defined(CONFIG_NUMA_BALANCING) && LAST_CPUPID_WIDTH == 0
#define LAST_CPUPID_NOT_IN_PAGE_FLAGS
#endif

#endif /* _LINUX_PAGE_FLAGS_LAYOUT */
