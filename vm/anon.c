/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "bitmap.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in(struct page *page, void *kva);
static bool anon_swap_out(struct page *page);
static void anon_destroy(struct page *page);

struct bitmap *swap_table;
const size_t SECTORS_PER_PAGE = PGSIZE / DISK_SECTOR_SIZE;

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
		.swap_in = anon_swap_in,
		.swap_out = anon_swap_out,
		.destroy = anon_destroy,
		.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void vm_anon_init(void)
{
	/* TODO: Set up the swap_disk. */
	swap_disk = disk_get(1, 1);
	// : 1 sector = 512bytes, 1 page = 4096bytes -> 1 slot = 8 sector
	disk_sector_t swap_size = disk_size(swap_disk) / SECTORS_PER_PAGE;
	swap_table = bitmap_create(swap_size); // 모든 비트들을 false로 초기화하는 함수, 사용되면 bit를 true로 바꾼다.
}

/* Initialize the file mapping */
bool anon_initializer(struct page *page, enum vm_type type, void *kva)
{
	/* Set up the handler */
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in(struct page *page, void *kva)
{
	struct anon_page *anon_page = &page->anon; // 해당 페이지를 anon으로 변경

	int page_no = anon_page->swap_index; // anon_page에 들어있는 swap_index

	if (bitmap_test(swap_table, page_no) == false)
	{
		return false;
	}
	for (int i = 0; i < SECTORS_PER_PAGE; i++)
	{
		disk_read(swap_disk, page_no * SECTORS_PER_PAGE + i, kva + DISK_SECTOR_SIZE * i);
	}
	bitmap_set(swap_table, page_no, false); // 스왑 슬롯이 더 더이상 사용중이지 않다 => 메모리에 올라갔다.
	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out(struct page *page)
{
	struct anon_page *anon_page = &page->anon;

	// 비트맵을 처음부터 순회해 false 값을 갖는 비트를 하나 찾는다.
	// 즉, 페이지를 할당 받을 수 있는 swap_slot을 하나 찾는다.
	int page_no = bitmap_scan(swap_table, 0, 1, false);
	if (page_no == BITMAP_ERROR)
	{
		return false;
	}

	// 한 페이지를 디스크에 써주기 위해 SECTORS_PER_PAGE 개의 섹터에 저장해야 한다.
	// 이 때 디스크에 각 섹터의 크기의 DISK_SECTOR_SIZE만큼 써준다.
	for (int i = 0; i < SECTORS_PER_PAGE; i++)
	{
		disk_write(swap_disk, page_no * SECTORS_PER_PAGE + i, page->va + DISK_SECTOR_SIZE * i);
	}

	// swap table의 해당 페이지에 대한 swap slot의 비트를 true로 바꿔주고(슬롯이 찼다고)
	// 해당 페이지가 PTE에서 present bit를 0으로 바꿔준다.
	// 이제 프로세스가 이 페이지에 접근하면 page fault가 뜬다.
	bitmap_set(swap_table, page_no, true);
	pml4_clear_page(thread_current()->pml4, page->va);

	// 페이지의 swap_index 값을 이 페이지가 저장된 swap slot의 번호로 써준다.
	anon_page->swap_index = page_no;
	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy(struct page *page)
{
	struct anon_page *anon_page = &page->anon;
}
