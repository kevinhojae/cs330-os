/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"
#include "lib/string.h"
#include "userprog/process.h"
#include "vm/uninit.h"

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

	// Copy the lazy load info for file backed page
	file_page->aux = page->uninit.aux;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	// file을 받아옴
	struct file_page *file_page = &page->file;
	// file의 aux에 저장된 lazy_load_info를 받아옴
	struct lazy_load_info *location_info = (struct lazy_load_info *) file_page->aux;
	// 해당 info를 기반으로 read_bytes 받아옴
	uint32_t want_bytes_size = location_info->read_bytes;

	// offset을 통해 읽기 시작할 위치 탐색, 이후 찾길 바라는 사이즈만큼 읽어옴
	uint32_t read_bytes =  (uint32_t) file_read_at(location_info->file, kva, want_bytes_size, location_info->ofs);

	// 읽어온 bytes의 수가 read_bytes와 같은지 확인
	if(read_bytes == want_bytes_size) {
		// 남는 메모리 공간 0으로 memory setting.
		memset(kva + want_bytes_size, 0, location_info->zero_bytes);
		return true;
	}
	else{
		// 메모리 공간 낭비를 막기 위해 할당 받은 페이지를 해제
		palloc_free_page(kva);
		return false;
	}

}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page = &page->file;
	struct thread *curr = thread_current();

	uint64_t *current_file_pml4 = curr->pml4;

	// check if the page is dirty
	if(pml4_is_dirty(current_file_pml4, page->va)){
		// if page is dirty, write back to file to reflect the changes to the file
		struct lazy_load_info *location_info = (struct lazy_load_info *) file_page->aux;
		file_write_at(location_info->file, page->va, location_info->read_bytes, location_info->ofs);
		pml4_set_dirty(current_file_pml4, page->va, false);
	}
	// free the file-backed page
	pml4_clear_page(current_file_pml4, page->va);
	page->frame = NULL;
	return true;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page = &page->file;

	struct lazy_load_info *info = file_page->aux;
	struct thread *curr = thread_current();

	// check if the page is dirty
	if (pml4_is_dirty(curr->pml4, page->va)) {
		// if page is dirty, write back to file to reflect the changes to the file
		file_write_at(info->file, page->va, info->read_bytes, info->ofs);
		// reset the dirty bit
		pml4_set_dirty(curr->pml4, page->va, 0);
	}

	// free the file-backed page
	pml4_clear_page(curr->pml4, page->va);
}

/** Do the mmap
 * Maps length bytes the file open as fd starting from offset byte into the process's virtual address space at addr.
 * The entire file is mapped into consecutive virtual pages starting at addr.
 * If the length of the file is not a multiple of PGSIZE, then some bytes in the final mapped page "stick out" beyond the end of the file.
 * Set these bytes to zero when the page is faulted in, and discard them when the page is written back to disk.
 * If successful, this function returns the virtual address where the file is mapped.
 * On failure, it must return NULL which is not a valid address to map a file.
 */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	void *return_addr = addr;
	struct file *open_file = file_reopen(file);

	if (open_file == NULL) {
		return NULL;
	}

	size_t read_byte = file_length(file) < length ? file_length(file) : length;
	size_t zero_byte = PGSIZE - read_byte % PGSIZE;

	while (read_byte > 0 || zero_byte > 0) {
		size_t page_read_bytes = read_byte < PGSIZE ? read_byte : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes; // 0 if not fragmented, page is fragmented in case of the last page

		struct lazy_load_info *info = (struct lazy_load_info *) malloc (sizeof(struct lazy_load_info));
		info->file = open_file;
		info->ofs = offset;
		info->read_bytes = page_read_bytes;
		info->zero_bytes = page_zero_bytes;

		if (!vm_alloc_page_with_initializer(VM_FILE, addr, writable, lazy_load_segment, info)) {
			return NULL;
		}

		read_byte -= page_read_bytes;
		zero_byte -= page_zero_bytes;
		addr += PGSIZE; // update addr to next page for mapping file in consecutive virtual pages
		offset += page_read_bytes;
	}

	return return_addr;
}

/** Do the munmap
 * Unmaps the mapping for the specified address range addr, which must be the virtual address returned by a previous call to mmap by the same process that has not yet been unmapped.
 */
void
do_munmap (void *addr) {
	// unmap the file-backed pages
	struct thread *curr = thread_current();

	while (true) {
		struct page *page = spt_find_page(&curr->spt, addr);
		if (page == NULL) {
			return NULL;
		}

		page->file.aux = page->uninit.aux;

		// free the file-backed page
		file_backed_destroy(page);
		addr += PGSIZE;
	}
}
