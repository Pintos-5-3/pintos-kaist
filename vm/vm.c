/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include <string.h>
#include "userprog/process.h"

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
	list_init(&frame_table);
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
/* 가상 주소 공간엔 페이지를 할당하고, 페이지 타입에 맞는 초기화 함수 설정
	1. uninit_page : lazy-loading을 위해 필요한 페이지 
	2. anon_page : 파일과 매핑되지 않은 페이지 (stack, heap)
	3. file_page : 파일과 매핑된 페이지 (code, data)
*/
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT);
	//페이지 타입이 UNINIT인지 확인 -> VM_UNINIT이 아닐 때 

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	/* spt 테이블에서 upage(va)와 매핑되는 페이지가 없다면 -> 새로운 페이지를 생성 */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		/*페이지 타입에 따라서 적절한 페이지 초기화 함수를 가져와서 새로운 struct page를 생성해야 한다.*/
		struct page *page = (struct page *)malloc(sizeof(struct page));

		bool (*page_initializer) (struct page *, enum vm_type, void *); 
		

		switch (VM_TYPE(type)){
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

		uninit_new(page, upage, init, type, aux, page_initializer);

		page->writable = writable; // 초기화 이후 별도로 writable 속성 설정 

		/* TODO: Insert the page into the spt. */
		/* spt에 새로 생성한 page를 넣는다. */
		// return spt_insert_page(spt, page);
		if (!spt_insert_page(spt, page)){
			return false;
		}
		return true;
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. 
spt에서 VA에 해당하는 것 찾으면 return page, 못찾으면 return NULL*/
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	// struct page *page = NULL;
	/* TODO: Fill this function. */

	
	//page의 VA와 spt의 VA가 일치하는 경우 
	struct page *temp_page = (struct page *)malloc(sizeof(struct page)); 
	struct hash_elem *e; 

	temp_page->va = pg_round_down(va); //주어진 가상 주소 va를 포함하는 페이지의 시작 주소 return 
	//주어진 주소를 페이지 크기 단위로 내림하여 해당 페이지의 시작 주소 구함 

	e = hash_find(&spt->spt_hash, &temp_page->hash_elem); //spt에서 hash값을 활용하여 페이지를 찾음 
	
	free(temp_page);

	//일치하는 페이지를 찾았으면 해당 페이지 return
	if (e != NULL) {
		return hash_entry(e, struct page, hash_elem);
	}
	else {
		return NULL; //해당 페이지를 찾지 못하는 경우
	}

}

/* Insert PAGE into spt with validation. 
가상 주소가 이미 spt에 존재하는지 확인 
- 존재한다면 삽입하지 않고, 존재하지 않으면 삽입 */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	// int succ = false;
	/* TODO: Fill this function. */

	return insert_page(&spt->spt_hash, page);

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

	struct thread *cur = thread_current();
	struct list_elem *frame_e;
	struct list_elem *e = ft_start;

	for (frame_e = list_begin(&frame_table); frame_e != list_end(&frame_table); frame_e = list_next(frame_e)){
		victim = list_entry(frame_e, struct frame, frame_elem);
		if (pml4_is_accessed(cur->pml4, victim->page->va))
			pml4_set_accessed(cur->pml4, victim->page->va, 0);
		else	
			return victim;
	} 

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */
	swap_out(victim->page);

	return victim;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/

//palloc_get_page 호출함 -> 새로운 물리메모리 페이지를 가져옴
//성공적으로 가져오면 frame 할당, frame 구조체의 멤버들을 초기화하고 해당 frame return
static struct frame *
vm_get_frame (void) {
	/* TODO: Fill this function. */

	struct frame *frame = (struct frame *)malloc(sizeof(struct frame));

	frame->kva = palloc_get_page(PAL_USER); //사용자 풀에서 메모리 할당을 위해 PAL_USER

	if (frame == NULL || frame->kva == NULL) {
		PANIC("TODO");
		return NULL;
		// frame = vm_evict_frame();
		// frame->page = NULL;
		// return frame;
	}

	list_push_back(&frame_table, &frame->frame_elem);
	
	frame->page = NULL; //물리 프레임을 할당받고 아직 매핑된 가상 페이지는 없으니까 NULL로 초기 설정을 해준다. 
	ASSERT(frame != NULL);
	ASSERT(frame->page == NULL);

	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
	/* stack에 해당하는 ANON 페이지를 UNINIT으로 만들고 SPT에 넣어준다.
	이후, 바로 claim해서 물리 메모리와 맵핑해준다. */
	vm_alloc_page(VM_ANON | VM_MARKER_0, pg_round_down(addr), 1);
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
/* 
page_fault로 부터 넘어온 인자
- not_present: 페이지 존재하지 않음(bogus fault)
- user: user에 의한 접근(true), 커널에 의한 접근(false)
- write: 쓰기 목적 접근(true), 읽기 목적 접근(false)
*/
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;

	struct page *page = NULL;
	/* TODO: Validate the fault. page fault 주소에 대한 유효성 검증*/
	/* TODO: Your code goes here */
	if (addr == NULL||is_kernel_vaddr(addr)) 
		return false;

	//접근한 페이지의 물리적 페이지가 존재하지 않는 경우 - page fault인 경우
	void *rsp = is_kernel_vaddr(f->rsp) ? thread_current()->rsp : f->rsp;	
	if (not_present) // 접근한 메모리의 physical page가 존재하지 않은 경우
    {
        /* TODO: Validate the fault */
        // 페이지 폴트가 스택 확장에 대한 유효한 경우인지를 확인한다.
        // void *rsp = f->rsp; // user access인 경우 rsp는 유저 stack을 가리킨다.
        // if (!user)            // kernel access인 경우 thread에서 rsp를 가져와야 한다.
        //     rsp = thread_current()->rsp;

        // 스택 확장으로 처리할 수 있는 폴트인 경우, vm_stack_growth를 호출한다.
        if (USER_STACK - (1 << 20) <= rsp - 8 && rsp - 8 == addr && addr <= USER_STACK)
            vm_stack_growth(addr);
        else if (USER_STACK - (1 << 20) <= rsp && rsp <= addr && addr <= USER_STACK)
            vm_stack_growth(addr);

        page = spt_find_page(spt, addr);
        if (page == NULL)
            return false;
        if (write == 1 && page->writable == 0) // write 불가능한 페이지에 write 요청한 경우
            return false;
        return vm_do_claim_page(page);
    }
    return false;
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
// va에 페이지 할당 
// 해당 페이지에 페이지 프레임 할당
// 먼저 한 페이지를 얻고, 그 이후에 해당 페이지를 인자로 갖는 vm_do_claim_page를 호출해야 함 
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: Fill this function */
	struct thread *cur = thread_current();
	
	//va에 해당하는 페이지를 현재 쓰레드의 spt에서 찾음 
	page = spt_find_page(&cur->spt, va);

	if (page == NULL) {
		return false;
	}

	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */

//가상 메모리 page와  물리 메모리 프레임 매핑
//vm_get_frame 호출해서 프레임을 하나 얻음
//MMU 세팅 -> 가상 주소와 물리 주소를 매핑한 정보를 페이지 테이블에 추가해야 함
//성공적인 연산 -> return true, 아니면 return false
static bool
vm_do_claim_page (struct page *page) {

	if (!page || page->frame) {
		return false;
	}

	struct frame *frame = vm_get_frame ();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	struct thread *cur = thread_current();
	pml4_set_page(cur->pml4, page->va, frame->kva, page->writable);
 
	//swap-in을 통해서 디스크의 스왑 영역에서 물리 메모리 프레임으로 load -> 스왑 작업 성공 여부 boolean 리턴
	return swap_in(page, frame->kva);
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {

	hash_init(&spt->spt_hash, page_hash, page_less, NULL);

}

/* Copy supplemental page table from src to dst 
src에서 dct까지의 spt 복사 */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
	// memcpy(&dst->spt_hash,&src->spt_hash,sizeof(struct hash));
	//return true;
	struct hash_iterator i;
    hash_first(&i, &src->spt_hash);
    while (hash_next(&i))
    {
        // src_page 정보
        struct page *src_page = hash_entry(hash_cur(&i), struct page, hash_elem);
        enum vm_type type = src_page->operations->type;
        void *upage = src_page->va;
        bool writable = src_page->writable;

        /* 1) type이 uninit이면 */
        if (type == VM_UNINIT)
        { // uninit page 생성 & 초기화
            vm_initializer *init = src_page->uninit.init;
            void *aux = src_page->uninit.aux;
            vm_alloc_page_with_initializer(VM_ANON, upage, writable, init, aux);
            continue;
        }

		if (type == VM_FILE)
        {
            struct lazy_load_arg *file_aux = malloc(sizeof(struct lazy_load_arg));
            file_aux->file = src_page->file.file;
            file_aux->ofs = src_page->file.ofs;
            file_aux->read_bytes = src_page->file.read_bytes;
            file_aux->zero_bytes = src_page->file.zero_bytes;
            if (!vm_alloc_page_with_initializer(type, upage, writable, NULL, file_aux))
                return false;
            struct page *file_page = spt_find_page(dst, upage);
            file_backed_initializer(file_page, type, NULL);
            file_page->frame = src_page->frame;
            pml4_set_page(thread_current()->pml4, file_page->va, src_page->frame->kva, src_page->writable);
            continue;
        }

        /* 2) type이 uninit이 아니면 */
        if (!vm_alloc_page(type, upage, writable)) // uninit page 생성 & 초기화
            // init이랑 aux는 Lazy Loading에 필요함
            // 지금 만드는 페이지는 기다리지 않고 바로 내용을 넣어줄 것이므로 필요 없음
            return false;

        // vm_claim_page으로 요청해서 매핑 & 페이지 타입에 맞게 초기화
        if (!vm_claim_page(upage))
            return false;

        // 매핑된 프레임에 내용 로딩
        struct page *dst_page = spt_find_page(dst, upage);
        memcpy(dst_page->frame->kva, src_page->frame->kva, PGSIZE);
    }
    return true;
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	/*pjt3   해시테이블 삭제 */ 
	// hash_destroy(&spt->spt_hash,hash_page_destroy);
	hash_clear(&spt->spt_hash,hash_page_destroy);
}


/*-----------------------------------------------------------------------*/
/*각 페이지의 가상 주소(Va)를 기반으로 해시 값 계산
-> 해시 테이블에서 해당 페이지를 빨리 찾을 수 있도록*/
unsigned page_hash (const struct hash_elem *p_, void *aux UNUSED){
	const struct page *p = hash_entry(p_, struct page, hash_elem);
	return hash_bytes(&p->va, sizeof(p->va)); 
	//p->va의 해시값 계산. 해시값 return
}

/*페이지들 간의 va 비교 - 키의 순서 결정*/
bool page_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED){
	const struct page *a = hash_entry(a_, struct page, hash_elem);
	const struct page *b = hash_entry(b_, struct page, hash_elem);

	return a->va < b->va;
}


static bool insert_page(struct hash *hash, struct page *page){
	if (!hash_insert(hash, &page->hash_elem)) {
		return true;
	}
	else {
		return false;
	}
}

static bool delete_page(struct hash *hash, struct page *page){
	if (!hash_delete(hash, &page->hash_elem))
		return true;
	else 
		return false;

}
void hash_page_destroy(struct hash_elem *e, void *aux)
{
    struct page *page = hash_entry(e, struct page, hash_elem);
    destroy(page);
    free(page);
}
