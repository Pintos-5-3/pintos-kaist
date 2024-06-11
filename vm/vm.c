/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "userprog/process.h"

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
		return spt_insert_page(spt, page); 		// 현재 쓰레드의 spt에 생성한 page를 넣어준다.
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
	free(page);
	return hash_entry(he, struct page, hash_elem);
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
	frame->page = NULL;

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
	// 스택의 크기를 증가시키기 위해 anonymous page를 하나 이상 할당하여 인자로 받은 주소(addr)가
	// 더 이상 예외 주소(faulted address)가 되지 않도록 한다.
	// 할당할 때 addr을 PGSIZE로 내림처리한다.
	vm_alloc_page(VM_ANON | VM_MARKER_0, pg_round_down(addr), 1);
	// VM_MARKER_0 : 현재 페이지가 스택을 위한 페이지임을 알림
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
	if(addr == NULL) {	// addr이 유효한지 확인
		return false;
	}
	if(is_kernel_vaddr(addr)) {		// addr이 kernel 영역의 주소인지 확인
		return false;
	}

	if(not_present) {		// 유효한 페이지 폴트(단순히 물리 메모리에 올라와있지 않은 페이지에 접근한 경우)
		/* TODO: Validate the fault */
		void *rsp = f->rsp;			// user mode에서의 접근이라면 rsp는 user stack을 가리킨다.
		if(!user) {					// kernel mode에서의 접근이라면 rsp는 kernel stack을 가리키므로
			rsp = thread_current()->rsp;		// thread에서 가져와야 한다.
		}

		// stack growth로 해결이 가능한 fault라면 vm_stack_growth를 호출하여 해결한다.
		if(USER_STACK - (1 << 20) <= rsp - 8 && rsp - 8 == addr && addr <= USER_STACK) {
			// PUSH instruction 인 경우를 대비하여 rsp - 8 까지는 허용
			vm_stack_growth(addr);
		}
		else if(USER_STACK - (1 << 20) <= rsp && rsp <= addr && addr <= USER_STACK) {
			// 일반적으로 허용되는 addr의 위치
			vm_stack_growth(addr);
		}

		page = spt_find_page(spt, addr);
		if(page == NULL) {			// 페이지가 존재하지 않는 경우
			return false;
		}
		if(write && !page->writable) {		// read-only page에 write을 시도하는 경우
			return false;
		}
		return vm_do_claim_page(page);		// page에 물리 frame을 할당
	}

	return false;			// 그 외에는 진짜 page fault(유효하지 않은 페이지에 접근하는 등의 경우)이므로 false
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
	pml4_set_page(cur->pml4, page->va, frame->kva, page->writable);
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
	struct hash_iterator i;				// spt_hash를 순회하기 위한 반복자
	hash_first(&i, &src->spt_hash);		// spt_hash의 맨 앞 원소를 가리키게 함
	while(hash_next(&i)) {
		// src_page 정보
		struct page *src_page = hash_entry(hash_cur(&i), struct page, hash_elem);
		enum vm_type type = src_page->operations->type;
		void *upage = src_page->va;
		bool writable = src_page->writable;

		// type이 uninit인 경우
		if(type == VM_UNINIT) {		// uninit page 생성 및 초기화
			vm_initializer *init = src_page->uninit.init;
			void *aux = src_page->uninit.aux;
			vm_alloc_page_with_initializer(VM_ANON, upage, writable, init, aux);
			continue;
		}

		// type이 uninit이 아니면
		if(!vm_alloc_page(type, upage, writable)) {	// uninit page 생성 및 알맞게 초기화
			// init과 aux는 lazy loading에 필요하고
			// 지금 만드는 페이지는 기다리지 않고 바로 내용을 넣어줄 것이므로 필요없음
			return false;
		}

		// vm_claim_page로 요청해서 매핑 및 페이지 타입에 맞게 초기화
		if(!vm_claim_page(upage)) {
			return false;
		}

		// 매핑된 프레임에 내용 로딩
		struct page *dst_page = spt_find_page(dst, upage);
		memcpy(dst_page->frame->kva, src_page->frame->kva, PGSIZE);
	}
	return true;
}

void hash_page_destroy(struct hash_elem *e, void *aux) {
	struct page *page = hash_entry(e, struct page, hash_elem);
	destroy(page);
	free(page);
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	hash_clear(&spt->spt_hash, hash_page_destroy);
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

