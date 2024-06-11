/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "vm/file.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "threads/mmu.h"
#include "threads/malloc.h"
#include "filesys/file.h"

static bool file_backed_swap_in(struct page *page, void *kva);
static bool file_backed_swap_out(struct page *page);
static void file_backed_destroy(struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
		.swap_in = file_backed_swap_in,
		.swap_out = file_backed_swap_out,
		.destroy = file_backed_destroy,
		.type = VM_FILE,
};

/* The initializer of file vm */
void vm_file_init(void)
{
}

/* Initialize the file backed page */
// 수정 사항을 파일에 다시 기록 하기 위해서는 매핑을 해제하는 시점에
// 해당 페이지에 매핑된 파일의 정보를 알 수 있어야 한다.
bool file_backed_initializer(struct page *page, enum vm_type type, void *kva)
{
	/* Set up the handler */
	// 핸들러 설정
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;

	/* (page struct의 일부정보)메모리를 지원하는 파일과 같은 페이지 구조에 대한 일부 정보를 업데이트할 수 있습니다.*/
	struct container *lazy_load_arg = (struct container *)page->uninit.aux;
	file_page->file = lazy_load_arg->file;
	file_page->ofs = lazy_load_arg->ofs;
	file_page->page_read_bytes = lazy_load_arg->page_read_bytes;
	file_page->page_zero_bytes = lazy_load_arg->page_zero_bytes;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in(struct page *page, void *kva)
{
	struct file_page *file_page UNUSED = &page->file;
	return lazy_load_segment(page, file_page); // 다시 메모리에 로드
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out(struct page *page)
{
	struct file_page *file_page UNUSED = &page->file;
	if (pml4_is_dirty(thread_current()->pml4, page->va))
	{
		// 수정사항(dirty bit == 1)은 파일에 업데이트
		file_write_at(file_page->file, page->va, file_page->page_read_bytes, file_page->ofs);
		// 이후 dirty bit를 0으로 바꾼다.
		pml4_set_dirty(thread_current()->pml4, page->va, 0);
	}

	// 페이지와 연결 끊기
	page->frame->page = NULL;
	page->frame = NULL;
	pml4_clear_page(thread_current()->pml4, page->va);
	return true;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
// 프로세스가 종료될 때 매핑이 해제되어야 한다.
// 수정사항 파일에 반영 후 가상 페이지 목록에서 페이지 제거
static void
file_backed_destroy(struct page *page)
{
	struct file_page *file_page UNUSED = &page->file;
	if (pml4_is_dirty(thread_current()->pml4, page->va))
	{
		// 수정사항(dirty bit == 1)은 파일에 업데이트
		file_write_at(file_page->file, page->va, file_page->page_read_bytes, file_page->ofs);
		// 이후 dirty bit를 0으로 바꾼다.
		pml4_set_dirty(thread_current()->pml4, page->va, 0);
	}
	// present bit를 0으로 만든다.
	pml4_clear_page(thread_current()->pml4, page->va);
}

/* Do the mmap */
// 실질적으로 가상 페이지를 할당해주는 함수(lazy_loading으로 할당)
// load segment와 유사한 로직
// 1. uninit 타입 페이지가 아닌 VM_FILE 타입으로 선언
// 2. 매핑이 끝난 후 연속된 유저 페이지의 첫 주소를 리턴
void *
do_mmap(void *addr, size_t length, int writable,
				struct file *file, off_t offset)
{

	void *start_addr = addr; // 매핑 성공시 반환할 첫 주소

	/* 파일에 대한 독립적인 참조
	 * 동일한 파일이 여러 프로세스에 의해 여러개의 매핑이 존재한다면,
	 * 한 매핑에서 파일의 내용을 변경하거나 제거하면 참조가 유효하지 않다
	 * 따라서 file_reopen을 통해 각 매핑 파일에 대한 개별적이고 독립적인 참조를 한다.	*/
	struct file *mfile = file_reopen(file);

	// 주어진 파일 길이와 length를 비교해서 length보다 크기가 작으면 파일을 통으로 읽고
	// 파일 길이가 더 크면 주어진 length만큼만 읽는다.
	size_t read_bytes = length > file_length(mfile) ? file_length(mfile) : length;
	// size_t read_bytes = length;
	// 마지막 페이지에 들어갈 자투리 0
	size_t zero_bytes = PGSIZE - read_bytes % PGSIZE;

	// read_bytes + zero_bytes 가 페이지 크기의 배수인지 확인
	ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
	// addr이 정렬되어 있는지 확인
	ASSERT(pg_ofs(addr) == 0);
	// offset이 페이지 정렬되어 있는지 확인
	ASSERT(offset % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0)
	{
		/* 이 페이지를 채우는 방법을 계산합니다.
			 파일에서 PAGE_READ_BYTES 바이트를 읽고
			 최종 PAGE_ZERO_BYTES 바이트를 0으로 채웁니다. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		struct container *lazy_load_arg = (struct container *)malloc(sizeof(struct container));
		lazy_load_arg->file = mfile;											// 내용이 담긴 파일 객체
		lazy_load_arg->ofs = offset;											// 이 페이지에서 읽기 시작한 위치
		lazy_load_arg->page_read_bytes = page_read_bytes; // 이 페이지에서 읽어야 하는 바이트 수
		lazy_load_arg->page_zero_bytes = page_zero_bytes; // 이 페이지에서 read_bytes만큼 읽고 공간이 남아 0으로 채워야 하는 바이트 수

		// 페이지 폴트가 발생했을 때 데이터를 로드하기 위한 준비(각 페이지에 필요한 메타 데이터 등을 설정)
		// 후에 페이지 폴트가 발생했을 때 로드된다.
		if (!vm_alloc_page_with_initializer(VM_FILE, addr, writable, lazy_load_segment, lazy_load_arg))
		{
			return NULL;
		}

		// 다음 페이지로 이동
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		addr += PGSIZE;
		offset += page_read_bytes;
	}
	return start_addr;
}

/* Do the munmap */
// 같은 파일이 매핑된 페이지가 모두 해제될 수 있도록 반복문 사용
// destroy 매크로로 연결되어 있는 file_backed_destroy함수에서 수정사항 반영
void do_munmap(void *addr)
{
	struct supplemental_page_table *spt = &thread_current()->spt;
	struct page *p = spt_find_page(spt, addr);
	while (true) // 매핑된 페이지가 모두 해제될때 까지
	{
		if (p == NULL)
		{
			return NULL;
		}
		else
		{
			destroy(p);
			addr += PGSIZE;								// 가상 주소 증가
			p = spt_find_page(spt, addr); // 페이지 찾기
		}
	}
}
