/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "lib/kernel/hash.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"

struct list frame_table;
struct lock frame_table_lock;

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
	lock_init(&frame_table_lock);
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

	bool success = false;
	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		
		// create the page
		struct page *page = (struct page *) malloc (sizeof(struct page));

		// fetch the initializer according to the VM type
		vm_initializer *initializer = NULL;
		switch (VM_TYPE(type)) {
			case VM_ANON:
				initializer = anon_initializer;
				break;
			case VM_FILE:
				initializer = file_backed_initializer;
				break;
			default:
				PANIC ("Invalid VM type");
		}

		uninit_new (page, upage, init, VM_TYPE (type), aux, initializer);
		page->writable = writable;

		/* TODO: Insert the page into the spt. */
		success = spt_insert_page (spt, page);
	}
	return success;
		
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va) {
	// 임의의 page 생성, 해당 사이즈만큼 malloc으로 메모리 할당
	// page의 hash_elem을 사용하여 va로 접근하고자 선언
	struct page *page = (struct page*)malloc(sizeof(struct page));
	/* TODO: Fill this function. */
	// hash list의 elem 사용을 위한 선언
	struct hash_elem *e;
	// input 받은 va의 시작 위치로 page round down 시켜서 (offset 0) 생성한 page->va에 저장 
	page->va = pg_round_down(va);
	// supplemental_page_table의 vm_entry_table에서 page->hash_elem을 찾아서 e에 저장
	e = hash_find(&spt->vm_entry_table, &page->hash_elem);
	// 용도를 다한 page 메모리 해제
	free(page);

	// e 받아오는 것 성공/실패한 경우 case 분류.
	if(e != NULL){
		// 성공시 page 구조체로 반환.
		return hash_entry(e, struct page, hash_elem);
	}
	else{
		// 실패할 경우 NULL 반환
		return NULL;
	}
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt,
		struct page *page) {
	int succ = false;
	/* TODO: Fill this function. */
	// hash insert 이용해서 page의 hash_elem을 supplemental_page_table의 vm_entry_table list에 넣어줌
	// 성공할 경우 NULL 포인터 반환
	succ = hash_insert(&spt->vm_entry_table, &page->hash_elem);
	if (succ == NULL){
		return true;
	}
	return false;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	
	hash_delete (&spt->vm_entry_table, &page->hash_elem);
	// TODO: 아래 dealloc 함수가 필요한지 잘 모르겠음. 해당 페이지를 dealloc할 필요가 있는지 test case를 통해 확인 필요. (순서도)
	//vm_dealloc_page (page);
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
	/* TODO: Fill this function. */
	
	// palloc_get_page()를 이용하여 single free page 할당 받음
	void *take_page = palloc_get_page(PAL_USER);

	// no available page인 경우, evict
	if(take_page == NULL){
		return vm_evict_frame();
	}

	// 성공적으로 page를 할당 받은 경우, 해당 page의 주소를 frame->kva에 저장
	// frane 구조체 생성, 해당 사이즈만큼 malloc으로 메모리 할당
	struct frame *frame = (struct frame *)malloc(sizeof(struct frame));
	frame->kva = take_page;

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

	// validate the fault
	// address가 user가 아니고 kernel인 경우, false 반환
	if (is_kernel_vaddr (addr)) {
		return false;
	}
	
	// page를 찾아서 page에 저장
	page = spt_find_page (spt, addr);
	// page가 존재하지 않는 경우, false 반환
	if (page == NULL) {
		return false;
	}

	// not_present인 경우, vm_do_claim_page 함수 호출하여 page claim
	if (not_present) {
		return vm_do_claim_page (page);
	}
	// write인 경우, vm_handle_wp 함수 호출하여 page write protect
	else if (write) {
		return vm_handle_wp (page);
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
bool
vm_claim_page (void *va UNUSED) {
	// 현재 thread의 spt를 확인하여(thread.h의 thread struct) 해당 va에 해당하는 page를 찾아서 page에 저장
	struct page *page = spt_find_page (&thread_current ()->spt, va);
	/* TODO: Fill this function */

	// 성공 실패 케이스 분류
	if(page != NULL){
		// 성공한 경우, vm_do_claim_page 함수 호출
		return vm_do_claim_page (page);
	}
	else{
		// 실패한 경우, false 반환
		return false;
	}
	
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	// vm_get_frame()을 통해 받아옴
	struct frame *frame = vm_get_frame ();
	
	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	// writable한 페이지인가 check
	bool writable = page->writable;

	// pml4_set_page()를 이용하여 page table entry를 설정
	bool mmu_set_ok = pml4_set_page (thread_current ()->pml4, page->va, frame->kva, writable);

	// 성공한 경우, frame_table list에 해당 page를 추가하고 swap_in 함수 호출로 마무리
	if(mmu_set_ok){
		lock_acquire(&frame_table_lock);
		list_push_back(&frame_table, &page->frame_table_elem);
		lock_release(&frame_table_lock);

		return swap_in (page, frame->kva);
	}
	else{
		return false;
	}
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt) {
	hash_init (&spt->vm_entry_table, page_hash, page_less, NULL);	
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
	// hash list의 elem 사용을 위한 선언
	struct hash_iterator i;

	// src의 vm_entry_table을 순회하며 각 page를 복사하여 dst의 vm_entry_table에 넣어줌
	hash_first (&i, &src->vm_entry_table);
	while (hash_next (&i)) {
		struct page *src_page = hash_entry (hash_cur (&i), struct page, hash_elem);
		struct page *dst_page = (struct page *) malloc (sizeof (struct page));
		*dst_page = *src_page;
		hash_insert (&dst->vm_entry_table, &dst_page->hash_elem);
	}
	return true;	
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	// hash list의 elem 사용을 위한 선언
	struct hash_iterator i;

	// spt의 vm_entry_table을 순회하며 각 page를 제거
	hash_first (&i, &spt->vm_entry_table);
	while (hash_next (&i)) {
		struct page *page = hash_entry (hash_cur (&i), struct page, hash_elem);
		destroy (page);
	}
}

unsigned
page_hash (const struct hash_elem *p_, void *aux) {
	const struct page *p = hash_entry (p_, struct page, hash_elem);
	return hash_bytes (&p->va, sizeof p->va);
}

bool
page_less (const struct hash_elem *a_, const struct hash_elem *b_,
		void *aux) {
	const struct page *a = hash_entry (a_, struct page, hash_elem);
	const struct page *b = hash_entry (b_, struct page, hash_elem);

	return a->va < b->va;
}