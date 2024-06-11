/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"
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
	struct file *f = file_reopen(file);
	void *start_addr = addr;	// 매핑 성공 시, 파일이 매핑된 가상 주소를 반환하는 데 사용
	int total_page_count = length <= PGSIZE ? 1 : length % PGSIZE ?
							length / PGSIZE + 1 : length / PGSIZE;
	// 이 매핑을 위해 사용한 총 페이지 수


	size_t read_bytes = file_length(f) < length ? file_length(f) : length;
	// 읽어야 하는 바이트 수 : f의 길이가 length보다 크면 f의 길이만큼, 아니면 length 만큼
	size_t zero_bytes = PGSIZE - read_bytes % PGSIZE;
	// 다 읽고 남은, 0으로 채워야하는 공간

	ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);	// 총 할당받은 크기가 page-aligned 되어있는지 확인
	ASSERT(pg_ofs(addr) == 0);	// upage가 page-aligned 되어있는지 확인
	ASSERT(offset % PGSIZE == 0);	// offset이 page-aligned 되어있는지 확인

	while(read_bytes > 0 || zero_bytes > 0) {
		/* 페이지를 채우는 양을 계산함 
		 * 파일에서 page_read_bytes 바이트를 읽고
		 * 최종 page_zero_bytes 바이트 만큼을 0으로 채운다.
		 */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		struct lazy_load_aux *lazy_load_aux = (struct lazy_load_aux *)malloc(sizeof(struct lazy_load_aux));
		lazy_load_aux->file = f;
		lazy_load_aux->ofs = offset;
		lazy_load_aux->read_bytes = page_read_bytes;
		lazy_load_aux->zero_bytes = page_zero_bytes;

		// vm_alloc_page_with_initializer 호출을 통해 대기 중인 객체를 생성한다.
		if(!vm_alloc_page_with_initializer(VM_FILE, addr, writable, lazy_load_segment, lazy_load_aux)) {
			return NULL;
		}

		struct page *p = spt_find_page(&thread_current()->spt, start_addr);
		p->mapped_page_count = total_page_count;

		// 읽은 바이트와 0으로 채운 바이트 수를 추적하고 가상 주소를 그만큼 증가시킴
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
}
