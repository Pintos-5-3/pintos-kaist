/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "userprog/process.h"

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
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	
	/*file_reopen을 통해 동일한 파일에 대해 다른 주소를 가지는 파일 구조체 생성
	 -> mmap하는 동안 외부에서 해당 파일을 close하는 불상사를 막기 위해서*/
	struct file *f = file_reopen(file); 

	void *start_addr = addr;

	//read_bytes: 파일에서 읽어와야 할 실제 데이터의 크기 
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


		if (!vm_alloc_page_with_initializer(VM_FILE, addr, writable, lazy_load_segment, lazy_load_arg))
			return NULL;
		
		struct page *p = spt_find_page(&thread_current()->spt, start_addr);


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
	while (true) {
		struct thread *cur = thread_current();
		//현재 쓰레드의 spt에서 addr 가상 주소에 해당하는 page를 찾는다. 
		struct page *page = spt_find_page(&cur-> spt, addr);

		if (page == NULL) {
			break;
		}

		struct lazy_load_arg *aux = (struct lazy_load_arg *)page->uninit.aux;

		//pml4_is_dirty: dirty bit가 1이면 true, 0이면 false 리턴
		
		if (pml4_is_dirty(cur->pml4, page->va)) {
			//페이지가 dirty한 경우(수정되었다면) - 변경된 데이터를 디스크 파일에 다시 업데이트
			file_write_at(aux->file, addr, aux->read_bytes, aux->ofs); 
			pml4_set_dirty(cur->pml4, page->va, 0); //dirty bit를 0으로 변경
		}

		pml4_clear_page(cur->pml4, page->va); //페이지의 present bit 값을 0으로 만들어주는 함수
		addr += PGSIZE; //다음 페이지 주소로 이동
	}
}
