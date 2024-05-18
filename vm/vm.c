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
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable, vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT);

	struct supplemental_page_table *spt = &thread_current ()->spt;

	bool success = false;
	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		
		// create the page
		// struct page *page = palloc_get_page (0);
		struct page *page = malloc(sizeof(struct page)); // palloc 대신 malloc을 써야 하는 이유는 palloc_get_page는 고정된 사이즈의 메모리만 할당해주기 때문에 page fault가 발생할 수 있음
		if(page==NULL){
			// page에 메모리 할당 못해줄 경우, 바로 false return
			return false;
		}

		// initializer의 type은 uninity_new 함수의 parameter 마지막 부분에서 확인 가능, 그대로 변경했음.
		bool (*initializer)(struct page *, enum vm_type, void *);

		switch (VM_TYPE(type)) {
			case VM_ANON:
				initializer = anon_initializer;
				break;
			case VM_FILE:
				initializer = file_backed_initializer;
				break;
			default:
				PANIC ("Invalid VM type");
				// 바로 false return 해줬습니다.
				return false;
		}

		// aux is lazy_load_info
		uninit_new (page, upage, init, type, aux, initializer);
		page->writable = writable;
		page->page_vm_type = type;

		/* TODO: Insert the page into the spt. */
		//lock_init(&page->page_lock);
		return spt_insert_page (spt, page);
	}
	return success;
		
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt, void *va) {
	// 임의의 page 생성, 해당 사이즈만큼 malloc으로 메모리 할당
	// page의 hash_elem을 사용하여 va로 접근하고자 선언
	struct page page;
	/* TODO: Fill this function. */
	// hash list의 elem 사용을 위한 선언
	struct hash_elem *e;
	// input 받은 va의 시작 위치로 page round down 시켜서 (offset 0) 생성한 page->va에 저장 
	// page = (struct page *) malloc (sizeof (struct page));
	page.va = pg_round_down(va);
	// supplemental_page_table의 vm_entry_table에서 page->hash_elem을 찾아서 e에 저장
	e = hash_find (&spt->vm_entry_table, &page.spt_elem);

	if (e == NULL) {
		return NULL;
	}

	return hash_entry (e, struct page, spt_elem);
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt,
		struct page *page) {
	int succ = false;
	/* TODO: Fill this function. */
	// hash insert 이용해서 page의 hash_elem을 supplemental_page_table의 vm_entry_table list에 넣어줌
	// 성공할 경우 NULL 포인터 반환
	succ = hash_insert(&spt->vm_entry_table, &page->spt_elem);
	if (succ == NULL){
		return true;
	}
	return false;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	// TODO: 아래 dealloc 함수가 필요한지 잘 모르겠음. 해당 페이지를 dealloc할 필요가 있는지 test case를 통해 확인 필요. (순서도)
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */
	
	struct list_elem *e;
	for (e = list_begin(&frame_table); e != list_end(&frame_table); e = list_next(e)) {
		victim = list_entry (e, struct frame, frame_elem);
		
		// victim의 page가 NULL인 경우, 해당 frame을 반환
		if (victim->page == NULL) {
			return victim;
		}
		
		if (pml4_is_accessed(thread_current()->pml4, victim->page->va)) {
			// page가 접근된 적이 있는 경우, 해당 frame의 page를 NULL로 설정하고, page의 access bit을 false로 설정
			pml4_set_accessed(thread_current()->pml4, victim->page->va, false);
		} else {
			// page가 접근된 적이 없는 경우, 해당 frame을 반환
			return victim;
		}
	}
	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */
	
	if (!swap_out (victim->page)) {
		return NULL;
	}

	return victim;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	/* TODO: Fill this function. */
	// 성공적으로 page를 할당 받은 경우, 해당 page의 주소를 frame->kva에 저장
	// frane 구조체 생성, 해당 사이즈만큼 malloc으로 메모리 할당
	void *kva = palloc_get_page (PAL_USER);

	if (kva == NULL) {
		struct frame *victim_frame = vm_evict_frame ();
		victim_frame->page = NULL;
		return victim_frame;
	}

	struct frame *frame = (struct frame *) malloc (sizeof (struct frame));

	frame->kva = kva;
	frame->page = NULL;

	list_push_back (&frame_table, &frame->frame_elem);

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr) {
	void *page_addr = pg_round_down(addr);
	bool alloc_succ, claim_succ = false;

	struct page *page;
	while((page = spt_find_page(&thread_current()->spt, page_addr)) == NULL){
		alloc_succ = vm_alloc_page(VM_ANON | VM_MARKER_0, page_addr, true);
		if (!alloc_succ) {
			PANIC("allocation page of stack growth failed");
		}

		claim_succ = vm_claim_page(page_addr);
		if (!claim_succ) {
			PANIC("claim page of stack growth failed");
		}

		memset(page_addr, 0, PGSIZE);
		page_addr += PGSIZE;
	}
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f , void *addr ,
		bool user , bool write , bool not_present ) {
	struct supplemental_page_table *spt = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */

	// validate the fault
	if (addr == NULL) {
		return false;
	}

	// address가 user가 아니고 kernel인 경우, false 반환
	if (is_kernel_vaddr (addr)) {
		return false;
	}
	
	// page를 찾아서 page에 저장
	// User program은 stack pointer 밑의 stack에 write할 경우 buggy함
	// stack pointer 보다 8 byte 아래에서 page fault가 발생할 수 있음
	uintptr_t stack_limit = USER_STACK - (1 << 20);
	uintptr_t rsp = user ? f->rsp : thread_current()->stack_pointer;
	uintptr_t stack_bottom = pg_round_down(rsp);
	if (addr >= rsp - 8 && addr <= USER_STACK && addr >= stack_limit) {
		vm_stack_growth (addr);
		return true;
	}

	page = spt_find_page (spt, addr);
	if (page == NULL) {
		return false;
	}

	if (!page->writable && write) {
		return false;
	}

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
	// 현재 thread의 spt를 확인하여(thread.h의 thread struct) 해당 va에 해당하는 page를 찾아서 page에 저장
	struct page *page = spt_find_page (&thread_current ()->spt, va);
	/* TODO: Fill this function */

	if (page == NULL) {
		return false;
	}

	return vm_do_claim_page(page);
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
	struct thread *current_thread = thread_current ();
	// if (pml4_get_page (current_thread->pml4, pg_round_down (page->va)) || !pml4_set_page (current_thread->pml4, pg_round_down (page->va), pg_round_down (frame->kva), page->writable)) {
	// 	return false;
	// }
	pml4_set_page (current_thread->pml4, pg_round_down (page->va), pg_round_down (frame->kva), page->writable);

	return swap_in (page, frame->kva);
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
	struct hash *src_spt = &src->vm_entry_table;
	struct hash_iterator i;

	hash_first(&i, src_spt);
	while (hash_next(&i)) {
		struct page *page = hash_entry (hash_cur(&i), struct page, spt_elem);
		if (page == NULL) {
			return false;
		}
		enum vm_type type = page_get_type (page);
		struct page *temp_page;

		if (page->operations->type == VM_UNINIT) {
			if (!vm_alloc_page_with_initializer (type, page->va, page->writable, page->uninit.init, page->uninit.aux)) {
				return false;
			}
		}
		else {
			if (!vm_alloc_page (type, page->va, page->writable)){
				return false;
			}
			if (!vm_claim_page (page->va)) {
				return false;
			}
			temp_page = spt_find_page (dst, page->va);
			memcpy (temp_page->frame->kva, page->frame->kva, PGSIZE);
		}
	}

	return true;
}

void
hash_elem_destroy(struct hash_elem *e, void *aux UNUSED) {
    struct page *p = hash_entry(e, struct page, spt_elem);
    // destroy(p);
    // palloc_free_page(p);
		vm_dealloc_page (p);
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	// spt의 vm_entry_table을 순회하며 각 page를 제거
	hash_clear (&spt->vm_entry_table, hash_elem_destroy);
}

unsigned
page_hash (const struct hash_elem *p_, void *aux) {
	const struct page *p = hash_entry (p_, struct page, spt_elem);
	return hash_bytes (&p->va, sizeof p->va);
}

bool
page_less (const struct hash_elem *a_, const struct hash_elem *b_,
		void *aux) {
	const struct page *a = hash_entry (a_, struct page, spt_elem);
	const struct page *b = hash_entry (b_, struct page, spt_elem);

	return a->va < b->va;
}