/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "userprog/process.h"
#include "userprog/syscall.h"
#include "threads/vaddr.h"

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
//파일에서 페이지의 내용을 읽어서 메모리에 로드 
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;

	return lazy_load_segment(page, file_page);
	// struct file_page *file_page UNUSED = &page->file;

	// int read = file_read_at(file_page->file, page->frame->kva, file_page->read_bytes, file_page->ofs);
	// memset(page->frame->kva + read, 0, PGSIZE - read);
	// return true;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;

	
	// 페이지가 수정되었는지 확인 dirty bit 
	if (pml4_is_dirty(thread_current()->pml4, page->va)) {
		//페이지 내용이 수정되었다면 파일에 다시 씀
		file_write_at(file_page->file, page->va, file_page->read_bytes, file_page->ofs);
		pml4_set_dirty(thread_current()->pml4, page->va, 0); //dirty bit 0으로 페이지가 더 이상 수정되지 않았음을 표시
	}
	page->frame->page = NULL; 
	page->frame = NULL;
	pml4_clear_page(thread_current()->pml4, page->va);
	return true;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;

	//pml4_is_dirty: dirty bit가 1이면 true, 0이면 false 리턴
	//페이지가 dirty한 경우(수정되었다면) - 변경된 데이터를 디스크 파일에 다시 업데이트
	if (pml4_is_dirty(thread_current()->pml4, page->va)){
		file_write_at(file_page->file, page->va, file_page->read_bytes, file_page->ofs); //페이지의 데이터를 디스크 파일에 씀
		pml4_set_dirty(thread_current()->pml4, page->va, 0); //dirty bit를 0으로 초기화하여 페이지가 더 이상 수정되지 않았음을 표시
	}

	 if (page->frame) {
        list_remove(&page->frame->frame_elem);
        page->frame->page = NULL;
        page->frame = NULL;
        free(page->frame);
    }

	pml4_clear_page(thread_current()->pml4, page->va); //페이지의 present bit 값을 0으로 만들어주는 함수 - 페이지가 더 이상 메모리에 존재하지 않음을 표시 
}

/* Do the mmap */
//메모리 매핑 -> 파일의 내용을 특정 메모리 주소에 매핑
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	
	lock_acquire(&filesys_lock);
	/*file_reopen을 통해 동일한 파일에 대해 다른 주소를 가지는 파일 구조체 생성
	 -> mmap하는 동안 외부에서 해당 파일을 close하는 불상사를 막기 위해서*/
	struct file *f = file_reopen(file); 

	void *start_addr = addr; //매핑을 시작할 주소
	
	/*총 페이지의 수 
	- length : 총 데이터의 길이 (바이트 수)
	- PGSIZE : 한 페이지의 크기 (바이트의 수)
	- 데이터의 길이가 한 페이지의 크기보다 작으면 총 페이지 수는 1 
								  크면 데이터를 페이지 크기로 나눴을 때 정확히 나눠 떨어지는지에 따라 다르게 계산 
	*/
	int total_page_count = length <= PGSIZE ? 1 : length % PGSIZE ? length / PGSIZE + 1
                                                                  : length / PGSIZE; 

	/* read_bytes: 파일에서 읽어와야 할 실제 데이터의 크기 
	- file_length(f) : 파일 f의 총 길이(바이트 수)
	- length: 읽어와야 하는 데이터의 목표 길이
	- file_length(f) < length : 파일의 길이만큼만 읽을 수 있다.
	- file_length(f) > length : 목표한 길이만큼 데이터를 읽어올 수 있다. 
	*/
	size_t read_bytes  = file_length(f) < length ? file_length(f) : length;
	
	//read_bytes가 PGSIZE 크기의 배수가 아닐 경우
	//마지막 페이지는 일부만 데이터로 채워지고 나머지는 0으로 채워져야 함 
	size_t zero_bytes = PGSIZE - read_bytes % PGSIZE; 
	
	while (read_bytes > 0 || zero_bytes > 0){

		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes: PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		struct lazy_load_arg *lazy_load_arg = (struct lazy_load_arg *)malloc(sizeof(struct lazy_load_arg));

		lazy_load_arg->file = f;
		lazy_load_arg->ofs = offset;
		lazy_load_arg->read_bytes = page_read_bytes;
		lazy_load_arg->zero_bytes = page_zero_bytes;

		//addr에 file-backed 페이지 할당
		//lazy_load_segment 함수의 인자로는 lazy_load_arg를
		if (!vm_alloc_page_with_initializer(VM_FILE, addr, writable, lazy_load_segment, lazy_load_arg))
			return NULL; //할당 실패시 return NULL
		
		struct page *p = spt_find_page(&thread_current()->spt, start_addr);
		p->mapped_page_count = total_page_count;


		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		addr += PGSIZE;
		offset += page_read_bytes;
	}
	lock_release(&filesys_lock);
	return start_addr;
}

/* Do the munmap */
//매핑된 메모리 해제
void
do_munmap (void *addr) {
	// while (true) {
	// 	struct thread *cur = thread_current();
	// 	//현재 쓰레드의 spt에서 addr 가상 주소에 해당하는 page를 찾는다. 
	// 	struct page *page = spt_find_page(&cur-> spt, addr);

	// 	if (page == NULL) {
	// 		break;
	// 	}

	// 	struct lazy_load_arg *aux = (struct lazy_load_arg *)page->uninit.aux;

	// 	if (pml4_is_dirty(cur->pml4, page->va)) {
	// 		file_write_at(aux->file, addr, aux->read_bytes, aux->ofs); 
	// 		pml4_set_dirty(cur->pml4, page->va, 0); //dirty bit를 0으로 변경
	// 	}

	// 	pml4_clear_page(cur->pml4, page->va); //페이지의 present bit 값을 0으로 만들어주는 함수
	// 	addr += PGSIZE; //다음 페이지 주소로 이동
	// }

	struct supplemental_page_table *spt = &thread_current()->spt;
	struct page *p = spt_find_page(spt, addr);
	int cnt = p->mapped_page_count;
	lock_acquire(&filesys_lock);

	//매핑된 페이지의 수만큼 반복
	for (int i = 0; i <cnt; i++){	
		if (p) {
			destroy(p);
		}
		addr += PGSIZE;
		p = spt_find_page(spt, addr);
	}
	lock_release(&filesys_lock);
}
