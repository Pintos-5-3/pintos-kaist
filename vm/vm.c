/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"

struct list frame_table;		// frame table 선언

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		struct page *page = (struct page *)malloc(sizeof(struct page));
		// page를 생성해준 후
		bool (*page_initializer)(struct page *, enum vm_type, void *);	// page를 init 해줄 initializer 선언
		switch(VM_TYPE(type)) {			// 입력받은 type 변수에서 VM page의 타입을 추출함
			case VM_ANON:
				page_initializer = anon_initializer;
				break;
			case VM_FILE:
				page_initializer = file_backed_initializer;
				break;
			default:
				free(page);
				break;
		}
		// 요청된 page type에 맞게 initializer를 fetch 해온다.

		uninit_new(page, upage, init, type, aux, page_initializer);
		// uninit_new 함수를 통해 page를 uninit 상태로 초기화해준다.

		page->writable = writable;
		// page의 writable 변수를 입력받은 인자로 바꿔준다.

		/* TODO: Insert the page into the spt. */
		if(spt_insert_page(spt, page)) {		// 현재 쓰레드의 spt에 생성한 page를 넣어준다.
			return true;
		}
		else {
			return false;
		}
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page *page = (struct page *)malloc(sizeof(struct page));
	/* TODO: Fill this function. */
	page->va = pg_round_down(va);		// 입력 받은 va를 이용하여 페이지 번호를 얻음
										// page 구조체의 va에 맞게 아래 비트들을 0으로 바꿔줘서 저장
	struct hash_elem *he = hash_find(&spt->spt_hash, &page->hash_elem);
	if(he == NULL) {
		free(page);
		page = NULL;
		return NULL;
	}
	page = hash_entry(he, struct page, hash_elem);

	return page;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	int succ = false;
	/* TODO: Fill this function. */
	succ = page_insert(&spt->spt_hash, page);

	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	struct frame *frame = NULL;
	/* TODO: Fill this function. */
	void *kva = palloc_get_page(PAL_USER);	// 일단 USER frame이므로 flag로 user로 설정

	if(kva == NULL) {		// 페이지(프레임) 할당 실패 시 메모리에 공간이 없다는 뜻이므로
		PANIC("todo");
		/* victim frame을 고르는 함수가 와야할듯? */

		return NULL;
	}

	frame = (struct frame *)malloc(sizeof(struct frame));
	// frame 구조체 자체가 page보다 작을 수 있으므로 크기가 변동적인 malloc으로 할당
	
	if(frame == NULL) {
		palloc_free_page(kva);
		PANIC("todo");
		/* 여기도 뭔가가 더 와야할 것 같음 */

		return NULL;
	}

	// frame 구조체 초기화
	frame->kva = kva;
	// frame->page = NULL;		// <= 이거 하면 밑에 ASSERT 걸리지 않나?

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */

	/* TODO: Your code goes here */

	return vm_do_claim_page (page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: Fill this function */
	struct thread *cur = thread_current();			// 현재 스레드의
	struct supplemental_page_table *spt = &cur->spt;	// spt를 가져와서
	page = spt_find_page(spt, va);		// va를 가진 page entry를 찾음
	if(page == NULL) {		// 못찾으면 할당 불가이므로 false return
		return false;
	}

	return vm_do_claim_page (page);	// 찾으면 frame을 할당하는 함수를 호출함
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	/* Set links */
	frame->page = page;
	page->frame = frame;
	// page와 frame을 매칭시키는 작업(MMU 세팅)

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	struct thread *cur = thread_current();
	pml4_set_page(cur->pml4, page, frame, page->writable);
	// 현재 쓰레드의 page table에 현재 연결한 page와 frame에 대한 정보를 담음

	return swap_in (page, frame->kva);		// swap in을 하며 끝남
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	hash_init(&spt->spt_hash, hash_func, page_less_func, NULL);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {

}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */

}

// project3 memory management (hash 초기화를 위한 함수)
/* hash function */
unsigned hash_func (const struct hash_elem *e, void *aux) {
	struct page *p = hash_entry(e, struct page, hash_elem);
	return hash_int((uint64_t)p->va);
}

/* hash bucket 내에서 어떤 기준으로 정렬시킬 지 위한 함수 */
static unsigned page_less_func (const struct hash_elem *a,
		const struct hash_elem *b,
		void *aux) {
	struct page *p_a = hash_entry(a, struct page, hash_elem);
	struct page *p_b = hash_entry(b, struct page, hash_elem);
	return (uint64_t)p_a->va > (uint64_t)p_b->va;
}

bool page_insert(struct hash *h, struct page *p) {
	if(!hash_insert(h, &p->hash_elem))	return true;
	else	return false;
}

bool page_delete(struct hash *h, struct page *p) {
	return hash_delete(h, &p->hash_elem);
}