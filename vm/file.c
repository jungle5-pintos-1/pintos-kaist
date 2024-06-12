/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "vm/file.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "threads/mmu.h"
#include "threads/malloc.h"
#include "filesys/file.h"
#include "threads/thread.h"

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
	struct lazy_load_arg *lazy_load_arg = (struct lazy_load_arg *)page->uninit.aux;

	file_page->file = lazy_load_arg->file;
	file_page->ofs = lazy_load_arg->ofs;
	file_page->read_bytes = lazy_load_arg->read_bytes;
	file_page->zero_bytes = lazy_load_arg->zero_bytes;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
	return lazy_load_segment(page, file_page); // 다시 메모리에 로드
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;

	if (page == NULL) {
		return false;
	}

	struct lazy_load_arg *aux = (struct lazy_load_arg *) page->uninit.aux;

	if (pml4_is_dirty(thread_current()->pml4, page->va)) {
		file_write_at(aux->file, page->va, aux->read_bytes, aux->ofs);
		pml4_set_dirty(thread_current()->pml4, page->va, 0);
	}

	page->frame->page = NULL;
	page->frame = NULL;
	pml4_clear_page(thread_current()->pml4, page->va);
	return true;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;	

	// PAGE의 dirty bit가 1이면 true, 아니면 false return
	if (pml4_is_dirty(thread_current()->pml4, page->va)) {

		// 물리 FRAME에 변경된 데이터를 다시 디스크 파일에 업데이트
		file_write_at(file_page->file, page->va, file_page->read_bytes, file_page->ofs);
		
		// PAGE의 dirty bit를 0으로 변경
		pml4_set_dirty(thread_current()->pml4, page->va, 0);
	}

	// PAGE의 present bit를 0으로 변경
	pml4_clear_page(thread_current()->pml4, page->va);
}

/* 가상 PAGE 할당 (Memory-Mapped File) */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {

	// reopen()를 이용해 새로운 파일 디스크립터를 얻음 (독립적인 mapping 위함)
	struct file *f = file_reopen(file);
	void *start_addr = addr;    // mapping 성공 시 파일이 mapping된 가상 주소를 반환하는 데 사용
	int total_page_count = length <= PGSIZE ? 1 : length % PGSIZE ? length / PGSIZE + 1 : length / PGSIZE; 

	size_t read_bytes = file_length(f) < length ? file_length(f) : length;
	size_t zero_bytes = PGSIZE - read_bytes % PGSIZE;

	ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT(pg_ofs(addr) == 0);
	ASSERT(offset % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		struct lazy_load_arg *lazy_load_arg = (struct lazy_load_arg *)malloc(sizeof(struct lazy_load_arg));

		lazy_load_arg->file = f;
		lazy_load_arg->ofs = offset;
		lazy_load_arg->read_bytes = page_read_bytes;
		lazy_load_arg->zero_bytes = page_zero_bytes;

		if (!vm_alloc_page_with_initializer(VM_FILE, addr, writable, lazy_load_segment, lazy_load_arg)) {
			return NULL;
		}

		struct page *p = spt_find_page(&thread_current()->spt, start_addr);
		p->mapped_page_count = total_page_count;    // 총 몇 PAGE를 사용해서 mapping이 이루어졌는지 개수 저장

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes; 
		addr += PGSIZE;
		offset += page_read_bytes;
	}

	return start_addr;
}

/* addr에 대한 mapping 해제 */
void
do_munmap (void *addr) {
	struct supplemental_page_table *spt = &thread_current()->spt;
	struct page *p = spt_find_page(spt, addr);
	int count = p->mapped_page_count;
	
	for (int i = 0; i < count; i++) {
		if (p) {
			// file_backed_destroy
			destroy(p);
		}
		
		addr += PGSIZE;
		p = spt_find_page(spt, addr);
	}
}
