#ifndef THREADS_VADDR_H
#define THREADS_VADDR_H

#include <debug.h>
#include <stdint.h>
#include <stdbool.h>

#include "threads/loader.h"

/* Functions and macros for working with virtual addresses.
 *
 * See pte.h for functions and macros specifically for x86
 * hardware page tables. */

#define BITMASK(SHIFT, CNT) (((1ul << (CNT)) - 1) << (SHIFT))

/* Page offset (bits 0:12). */
#define PGSHIFT 0                          /* Index of first offset bit. 페이지 오프셋 비트의 시작 인덱스 - 0에서 시작*/
#define PGBITS  12                         /* Number of offset bits. 페이지 오프셋에 사용되는 비트 수 - 12 비트 페이지의 크기는 2^12 -> 4KB*/
#define PGSIZE  (1 << PGBITS)              /* Bytes in a page. 페이지 크기를 바이트 단위로 정의 -> 4KB*/
#define PGMASK  BITMASK(PGSHIFT, PGBITS)   /* Page offset bits (0:12). 페이지 오프셋 비트를 마스킹하기 위한 값 -> 페이지 내에서 오프셋 결정하는 데 사용됨*/

/* Offset within a page. */
#define pg_ofs(va) ((uint64_t) (va) & PGMASK)

#define pg_no(va) ((uint64_t) (va) >> PGBITS)

/* Round up to nearest page boundary. */
#define pg_round_up(va) ((void *) (((uint64_t) (va) + PGSIZE - 1) & ~PGMASK))

/* Round down to nearest page boundary. */
#define pg_round_down(va) (void *) ((uint64_t) (va) & ~PGMASK) //특정 주소가 속한 페이지의 시작 주소 쉽게 계산

/* Kernel virtual address start */
#define KERN_BASE LOADER_KERN_BASE

/* User stack start */
#define USER_STACK 0x47480000

/* Returns true if VADDR is a user virtual address. */
#define is_user_vaddr(vaddr) (!is_kernel_vaddr((vaddr)))

/* Returns true if VADDR is a kernel virtual address. */
#define is_kernel_vaddr(vaddr) ((uint64_t)(vaddr) >= KERN_BASE)

// FIXME: add checking
/* Returns kernel virtual address at which physical address PADDR
 *  is mapped. */
#define ptov(paddr) ((void *) (((uint64_t) paddr) + KERN_BASE))

/* Returns physical address at which kernel virtual address VADDR
 * is mapped. */
#define vtop(vaddr) \
({ \
	ASSERT(is_kernel_vaddr(vaddr)); \
	((uint64_t) (vaddr) - (uint64_t) KERN_BASE);\
})

#endif /* threads/vaddr.h */
