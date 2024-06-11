/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "lib/kernel/bitmap.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/*----------Swap In/out------------*/
const size_t SECTORS_PER_PAGE = PGSIZE / DISK_SECTOR_SIZE; /*한 페이지가 몇개의 섹터로 구성되는지*/


/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
/*anonymous page를 위한 swap disk 초기화하는 함수
- 스왑 디스크 설정, 스왑 테이블 초기화
- sector: 디스크 저장 장치의 물리적 저장 단위 - 보통 1개의 섹터 512 byte
- slot: 스왑 디스크에서 페이지를 저장할 수 있는 공간, PGSIZE와 동일한 크기
- swap table: 메모리 페이지가 스왑 디스크의 어느 슬롯에 저장되어 있는지를 추적하기 위한 자료구조
*/
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	
	swap_disk = disk_get(1,1); //(1,1) 위치의 스왑 디스크를 가져온다
	//스왑 테이블 초기화
	list_init(&swap_table);
	lock_init(&swap_table_lock);

	/* 
	스왑 디스크의 총 크기를 페이지 단위로 계산 
		SECTORS_PER_PAGE = 4096 / 512 byte = 8 한 페이지는 8개의 디스크 섹터로 구성 
		예) 총 섹터수가 10240개이고 한 페이지가 8개 섹터로 구성되면 스왑 디스크는 총 1280개의 페이지 저장 가능
	*/
	disk_sector_t swap_size = disk_size(swap_disk) / SECTORS_PER_PAGE;
	
	//스왑 테이블 설정
	for (disk_sector_t i = 0; i < swap_size; i++){
		struct slot *slot = (struct slot *)malloc(sizeof(struct slot));
		slot->page = NULL; // 슬롯의 페이지를 NULL로 초기화
		slot->slot_no = i; //슬롯 번호 설정

		lock_acquire(&swap_table_lock);
		list_push_back(&swap_table, &slot->swap_elem); //스왑 테이블의 끝에 슬롯을 추가
		lock_release(&swap_table_lock);
	}
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;

	anon_page->slot_no = -1;
	return true;
}

/* Swap in the page by read contents from the swap disk. 
디스크에서 메모리로 데이터 내용을 읽어서 Swap disk에서 anon page를 swap in*/
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;

	disk_sector_t page_slot_no = anon_page->slot_no; //스왑 디스크에서 페이지가 저장된 슬롯 번호
	// 0이면 첫 번째 페이지, 1이면 두 번째 페이지

	struct list_elem *e; // Swap table을 순회하기 위한 iterator e
	struct slot *slot;
	
	lock_acquire(&swap_table_lock); //swap table 접근을 위한 lock 획득
	for (e = list_begin(&swap_table); e != list_end(&swap_table); e = list_next(e)){
		slot = list_entry(e, struct slot, swap_elem);

		//슬롯 번호가 일치하는 경우
		if (slot->slot_no == page_slot_no){
			//해당 슬롯에서 데이터를 읽어서 물리 메모리로 가져온다.
			for (int i = 0; i < SECTORS_PER_PAGE; i ++) {
				disk_read(swap_disk, page_slot_no * SECTORS_PER_PAGE + i, kva + DISK_SECTOR_SIZE *i); 
				// page_slot_no * SECTORS_PER_PAGE = 해당 페이지의 첫 번째 섹터의 번호 
				// page_slot_no * SECTORS_PER_PAGE + i => 현재 읽어야 할 섹터의 번호
				// kva + DISK_SECTOR_SIZE * i = 페이지의 시작 주소 + 하나의 섹터 크기 * 섹터의 인덱스
				// 첫 번쨰 섹터는 kva, 두 번째 섹터는 kva + 512에 저장된다.
				
			}
			slot->page = NULL; 			//슬롯의 페이지 포인터 NULL로 초기화
			anon_page->slot_no = -1;    //anon_page의 slot_no -1로 초기화
			lock_release(&swap_table_lock);

			return true;
		}
	}
	lock_release(&swap_table_lock);
	return false;
}

/* Swap out the page by writing contents to the swap disk. 
메모리의 내용을 디스크로 복사해서 anon page를 swap disk로 Swap out
=> 먼저 사용 가능한 스왑 슬롯을 찾은 다음, 데이터 페이지를 슬롯에 복사한다. 메모리에서 해제 */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	struct list_elem *e;
	struct slot *slot;
	// bool success =false;

	lock_acquire(&swap_table_lock);

	//스왑 테이블의 모든 슬롯 순회
	for (e = list_begin(&swap_table); e != list_end(&swap_table); e = list_next(e)) {
		slot = list_entry(e, struct slot, swap_elem);
		//슬롯이 비어있는지 확인
		if (slot->page == NULL) {	
			int slot_no = slot->slot_no; //빈 슬롯의 번호를 가져옴 

			// 페이지의 모든 섹터를 swap disk에 쓴다. 
			for (int i = 0; i <SECTORS_PER_PAGE; ++i){
				disk_write(swap_disk, slot_no * SECTORS_PER_PAGE + i, page-> va + DISK_SECTOR_SIZE * i);
			}
			anon_page->slot_no = slot->slot_no; // anon_page에 슬롯 번호 저장
			slot->page = page; //슬롯과 페이지 연결

			//페이지와 프레임의 연결 해제
			page->frame->page = NULL;
			page->frame = NULL;
			pml4_clear_page(thread_current()->pml4, page->va); //페이지 테이블에 서 페이지 제거
			lock_release(&swap_table_lock);
			return true;
		}
	}
	lock_release(&swap_table_lock);
	// return success;
	PANIC("Swap out failed : insufficient swap space available");
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	struct list_elem *e; 
	struct slot *slot;

	lock_acquire(&swap_table_lock);

	//스왑 테이블의 모든 슬롯 순회
	for (e = list_begin(&swap_table); e != list_end(&swap_table); e = list_next(e)){
		slot = list_entry(e, struct slot, swap_elem);
		//슬롯이 현재 페이지에 해당하면 페이지 포인터 NULL로 설정
		if (slot->slot_no == anon_page->slot_no){
			slot->page = NULL;
			break;
		}
	}
	lock_release(&swap_table_lock);
}
