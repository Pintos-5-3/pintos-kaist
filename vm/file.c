/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "userprog/process.h"
#include <stdio.h>
#include "vm/file.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;

	struct lazy_load_arg *lazy_load_arg = (struct lazy_load_arg *)page->uninit.aux;
    file_page->file = lazy_load_arg->file;
    file_page->ofs = lazy_load_arg->ofs;
    file_page->read_bytes = lazy_load_arg->read_bytes;
    file_page->zero_bytes = lazy_load_arg->zero_bytes;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
	if (pml4_is_dirty(thread_current()->pml4, page->va))
    {
        file_write_at(file_page->file, page->va, file_page->read_bytes, file_page->ofs);
        pml4_set_dirty(thread_current()->pml4, page->va, 0);
    }
    pml4_clear_page(thread_current()->pml4, page->va);
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable, struct file *file, off_t offset) {
	// // printf("do_mmap 까지옴\n");
	// struct file *mfile = file_reopen(file);
  	// void * start_addr = addr; // 시작 주소

  	// /* 주어진 파일 길이와 length를 비교해서 length보다 file 크기가 작으면 파일 통으로 싣고 파일 길이가 더 크면 주어진 length만큼만 load*/
  	// size_t read_bytes = length > file_length(file) ? file_length(file) : length;
  	// size_t zero_bytes = PGSIZE - read_bytes % PGSIZE; // 마지막 페이지에 들어갈 자투리 바이트

	// /* 파일을 페이지 단위로 잘라 해당 파일의 정보들을 container 구조체에 저장한다.
	//    FILE-BACKED 타입의 UINIT 페이지를 만들어 lazy_load_segment()를 vm_init으로 넣는다. */
	// while (read_bytes > 0 || zero_bytes > 0) {
	// 	size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
	// 	size_t page_zero_bytes = PGSIZE - page_read_bytes;

	// 	struct lazy_load_arg *container = (struct lazy_load_arg*)malloc(sizeof(struct lazy_load_arg));
	// 	container->file = mfile;
	// 	container->ofs = offset;
	// 	container->read_bytes = page_read_bytes;
	// 	// 여기서는 페이지 할당을 FILE-BACKED로 해줘야 하니 아래 VM_FILE로 넣어준다.
	// 	if (!vm_alloc_page_with_initializer (VM_FILE, addr, writable, lazy_load_segment, container)) {
	// 		// printf("뭔가 실패함\n");	
	// 		return NULL;
    // 	}
	// 	//다음 페이지로 이동
	// 	read_bytes -= page_read_bytes;
	// 	zero_bytes -= page_zero_bytes;
	// 	addr       += PGSIZE;
	// 	offset     += page_read_bytes;
	// }
	// // 최종적으로는 시작 주소를 반환
	// printf("반복문 끝냄\n");
	// return start_addr;
	struct file *f = file_reopen(file);
    void *start_addr = addr; // 매핑 성공 시 파일이 매핑된 가상 주소 반환하는 데 사용
    // 이 매핑을 위해 사용한 총 페이지 수
    int total_page_count = length <= PGSIZE ? 1 : length % PGSIZE ? length / PGSIZE + 1
                                                                  : length / PGSIZE; 

    size_t read_bytes = file_length(f) < length ? file_length(f) : length;
    size_t zero_bytes = PGSIZE - read_bytes % PGSIZE;

    ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
    ASSERT(pg_ofs(addr) == 0);      // upage가 페이지 정렬되어 있는지 확인
    ASSERT(offset % PGSIZE == 0); // ofs가 페이지 정렬되어 있는지 확인

    while (read_bytes > 0 || zero_bytes > 0)
    {
        /* 이 페이지를 채우는 방법을 계산합니다.
        파일에서 PAGE_READ_BYTES 바이트를 읽고
        최종 PAGE_ZERO_BYTES 바이트를 0으로 채웁니다. */
        size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
        size_t page_zero_bytes = PGSIZE - page_read_bytes;

        struct lazy_load_arg *lazy_load_arg = (struct lazy_load_arg *)malloc(sizeof(struct lazy_load_arg));
        lazy_load_arg->file = f;
        lazy_load_arg->ofs = offset;
        lazy_load_arg->read_bytes = page_read_bytes;
        lazy_load_arg->zero_bytes = page_zero_bytes;

        // vm_alloc_page_with_initializer를 호출하여 대기 중인 객체를 생성합니다.
        if (!vm_alloc_page_with_initializer(VM_FILE, addr,
                                            writable, lazy_load_segment, lazy_load_arg))
            return NULL;

        struct page *p = spt_find_page(&thread_current()->spt, start_addr);
        p->mapped_page_count = total_page_count;

        /* Advance. */
        // 읽은 바이트와 0으로 채운 바이트를 추적하고 가상 주소를 증가시킵니다.
        read_bytes -= page_read_bytes;
        zero_bytes -= page_zero_bytes;
        addr += PGSIZE;
        offset += page_read_bytes;
    }

    return start_addr;
}

/* Do the munmap */
void
do_munmap (void *addr) {
	// printf("do_munmap 들어옴\n");
	// while (true) {
    //     struct page* page = spt_find_page(&thread_current()->spt, addr);
        
    //     if (page == NULL)
    //         break;

    //     struct lazy_load_arg * aux = (struct lazy_load_arg *) page->uninit.aux;
        
    //     // dirty(사용되었던) bit 체크
    //     if(pml4_is_dirty(thread_current()->pml4, page->va)) {
    //         file_write_at(aux->file, addr, aux->read_bytes, aux->ofs);
    //         pml4_set_dirty (thread_current()->pml4, page->va, 0);
    //     }

    //     pml4_clear_page(thread_current()->pml4, page->va);
    //     addr += PGSIZE;
    // }
	// printf("do_munmap 끝냄\n");
	struct supplemental_page_table *spt = &thread_current()->spt;
    struct page *p = spt_find_page(spt, addr);
    int count = p->mapped_page_count;
    for (int i = 0; i < count; i++)
    {
        if (p)
            destroy(p);
        addr += PGSIZE;
        p = spt_find_page(spt, addr);
    }
}
