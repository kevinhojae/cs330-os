/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

// anon page는 백업 디스크가 따로 존재하지 않으므로 위에서 swap disk 공간을 따로 정의
// 그 공간 사용을 위한 구조체를 만들어준다. - 사용 여부, lock
static struct page_swap anon_page_swap;

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	// swap disk에 disk get함수를 이용하여 해당 함수에서 정의한 (1, 1) 입력을 통해 swap 전용 디스크 부여
	swap_disk = disk_get (1, 1);
	// swap_disk의 크기(byte return -> 한 개 page 크기는 8byte)를 이용하여 swap_map을 생성
	anon_page_swap.swap_map = bitmap_create(disk_size(swap_disk)/8);

	// swap_lock 초기화
	lock_init(&anon_page_swap.swap_lock);
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	//page의 operation을 anon_ops로 설정
	page->operations = &anon_ops;

	// anon_page 구조체를 새로 생성하고 입력받은 page의 anon page 정보를 입력
	struct anon_page *anon_page = &page->anon;

	// parameter로 주어진 값 그대로 배정
	anon_page->type = type;
	anon_page->va = kva;
	// initializing 할 때 swap_table_index를 -1로 초기화, 나중에 swap_out할 때 이 값이 업데이트 됨.
	anon_page->swap_table_index = -1;
	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
	
	// 현재 swap_table_index 값 받아오기.
	unsigned int index_value = anon_page->swap_table_index;

	// 저장 위치가 올바른가 체크
	if(bitmap_test(anon_page_swap.swap_map, index_value) == false){
		//PANIC("bitap test failed for swap_table in anon_swap_in");
		return false;
	}

    // swap disk에 있는 페이지를 1 Byte씩 read
    for(int i = 0; i < 8; i++){
        disk_read(swap_disk, index_value * 8 + i, kva + i * DISK_SECTOR_SIZE);
	}	

	// 해당 swap table에 비트맵 설정하기
	bitmap_set_multiple(anon_page_swap.swap_map, index_value, 1, false);

	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	
	// bitmap 접근 시 lock 걸기
	lock_acquire(&anon_page_swap.swap_lock);

	// bitmap에서 정보 받아와서 swap_table_index값을 업데이트
	// swap_out 되어 table에 저장될 위치를 받아오고, 해당 저장 위치의 bit에 사용중 표시를 한 것.
	unsigned int index_value = bitmap_scan_and_flip(anon_page_swap.swap_map, 0, 1, false);
	anon_page->swap_table_index = index_value;

	//lock 해제
	lock_release(&anon_page_swap.swap_lock);

	// 하나의 page를 disk에 옮겨적기 위한 과정.
	// sector 1개가 8의 크기이기에 8번 반복
	for(int i = 0; i < 8; i++){
		// swap_disk에서 해당 index에 8개의 sector를 write
		disk_write(swap_disk, index_value * 8 + i, page->frame->kva + i * DISK_SECTOR_SIZE);
	}

	// pml4에서 해당 page를 clear
	pml4_clear_page(thread_current()->pml4, page->va);
	// page의 (swap_out했으니) 프레임을 null로 설정.
	page->frame = NULL;

	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	
}
