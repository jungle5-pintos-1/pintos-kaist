/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "lib/kernel/hash.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"
#include "vm/uninit.h"

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void vm_init(void)
{
	vm_anon_init();
	vm_file_init();
#ifdef EFILESYS /* For project 4 */
	pagecache_init();
#endif
	register_inspect_intr();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
	list_init(&frame_table);
	lock_init(&frame_table_lock);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type(struct page *page)
{
	int ty = VM_TYPE(page->operations->type);
	switch (ty)
	{
	case VM_UNINIT:
		return VM_TYPE(page->uninit.type);
	default:
		return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim(void);
static bool vm_do_claim_page(struct page *page);
static struct frame *vm_evict_frame(void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
/* pending 중인 페이지 객체를 초기화하고 생성
 * 페이지를 생성하려면 직접 생성하지 않고 이 함수나 vm_alloc_page 사용
 * init과 aux는 첫 page fault가 발생할 때 호출 */
bool vm_alloc_page_with_initializer(enum vm_type type, void *upage, bool writable,
																		vm_initializer *init, void *aux)
{

	ASSERT(VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current()->spt;

	/* Check wheter the upage is already occupied or not. */
	// upage(page를 할당할 가상주소)가 이미 사용 중인지 확인
	if (spt_find_page(spt, upage) == NULL)
	{
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		// 1) 페이지를 생성하고,
		struct page *p = (struct page *)malloc(sizeof(struct page));

		// 2) type에 따라 초기화 함수를 가져와서,
		bool (*page_initializer)(struct page *, enum vm_type, void *);
		switch (VM_TYPE(type))
		{
		case VM_ANON:
			page_initializer = anon_initializer;
			break;
		case VM_FILE:
			page_initializer = file_backed_initializer;
			break;
		}

		// 3) "uninit" 타입의 페이지로 초기화한다.
		uninit_new(p, upage, init, type, aux, page_initializer);

		// 필드 수정은 uninit_new 호출 뒤에
		p->writable = writable;

		/* TODO: Insert the page into the spt. */
		// 4) 생성한 페이지를 spt에 추가
		return spt_insert_page(spt, p);
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
/* 해시 테이블에서 인자로 받은 va가 있는지 찾는 함수.
	 va가 속해 있는 페이지가 spt에 있다면 이를 리턴 */
struct page *
spt_find_page(struct supplemental_page_table *spt UNUSED, void *va UNUSED)
{
	struct page *page = NULL;
	/* TODO: Fill this function. */
	page = malloc(sizeof(struct page));
	struct hash_elem *e;

	// 해당 va가 속해있는 페이지 시작 주소를 갖는 페이지를 만든다.
	page->va = pg_round_down(va);
	e = hash_find(&spt->spt_hash, &page->hash_elem); // hash_elem을 리턴해준다.
	free(page);

	return e != NULL ? hash_entry(e, struct page, hash_elem) : NULL;
}

/* Insert PAGE into spt with validation. */
/* supplementary page table에 struct page를 삽입.*/
bool spt_insert_page(struct supplemental_page_table *spt UNUSED,
										 struct page *page UNUSED)
{
	// int succ = false;
	/* TODO: Fill this function. */
	// 가상 주소가 spt에 존재하면 삽입 x, 존재하지 않으면 삽입
	return hash_insert(&spt->spt_hash, &page->hash_elem) == NULL ? true : false;
}

void spt_remove_page(struct supplemental_page_table *spt, struct page *page)
{
	vm_dealloc_page(page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim(void)
{
	struct frame *victim = NULL;
	/* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame(void)
{
	struct frame *victim UNUSED = vm_get_victim();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
/*palloc을 이용해 프레임을 할당 받아온다.
	가용 가능한 페이지가 없다면 페이지를 스왑후 이를 반환 -> 이후에 구현 처음에는 PANIC("todo") 이용
	즉, 항상 유효한 물리 주소를 반환하는 함수
	유저 풀이 꽉 차있다면, 이 함수는 가용 가능한 메모리 공간을 얻기 위해 frame 공간을 디스크로 내린다.*/
/*frame_table? : 빈 프레임이 연결되어 있는 연결리스트
	특정 프레임을 찾을 필요없이 그냥 빈 프레임 찾아서 가상 페이지랑 연결하면 된다
	따라서 FIFO */
static struct frame *
vm_get_frame(void)
{
	struct frame *frame = NULL;
	frame = malloc(sizeof(struct frame));
	/* TODO: Fill this function. */

	void *kva = palloc_get_page(PAL_USER);
	if (kva == NULL)
	{
		PANIC("todo");
	}
	frame->kva = kva; // user pool에서 커널 가상 주소 공간으로 1page 할당
	frame->page = NULL;

	// lock_acquire(&frame_table_lock);
	// list_push_back(&frame_table, &frame->frame_elem);
	// lock_release(&frame_table_lock);
	ASSERT(frame != NULL);
	ASSERT(frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth(void *addr UNUSED)
{
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp(struct page *page UNUSED)
{
}

/* Return true on success */
// page fault가 발생하면 제어권을 받는 함수
// 할당된 물리 프레임이 존재하지 않을 때 예외는 not_present에 true 전달
bool vm_try_handle_fault(struct intr_frame *f UNUSED, void *addr UNUSED,
												 bool user UNUSED, bool write UNUSED, bool not_present UNUSED)
{
	struct supplemental_page_table *spt UNUSED = &thread_current()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */

	if (addr == NULL)
	{
		return false;
	}

	if (is_kernel_vaddr(addr))
	{
		return false;
	}

	if (not_present) // 접근한 메모리의 frame이 존재하지 않은 경우
	{
		page = spt_find_page(spt, addr); // 해당 주소에 해당하는 페이지 있는지 확인
		if (page == NULL)
		{
			return false; // 없으면 false
		}
		if (write == 1 && page->writable == 0) // read-only page에 write를 한 경우
		{
			return false;
		}
		return vm_do_claim_page(page); // 있으면 frame 할당 요청
	}

	return false;
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void vm_dealloc_page(struct page *page)
{
	destroy(page);
	free(page);
}

/* Claim the page that allocate on VA. */
// spt에서 va에 해당하는 페이지를 가져와서 frame과 매핑을 요청한다.(page와 frame을 연결요청)
bool vm_claim_page(void *va UNUSED)
{
	struct page *page = NULL;
	/* TODO: Fill this function */
	// spt에서 va에 해당하는 페이지 찾기
	page = spt_find_page(&thread_current()->spt, va);
	if (page == NULL)
	{
		return false;
	}

	return vm_do_claim_page(page);
}

/* Claim the PAGE and set up the mmu. */
// 새 frame을 가져와서 page와 매핑
static bool
vm_do_claim_page(struct page *page)
{
	struct frame *frame = vm_get_frame(); // 프레임을 가져온다

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	// 가상 주소와 물리 주소를 매핑
	struct thread *cur = thread_current();
	if (!pml4_set_page(cur->pml4, page->va, frame->kva, page->writable))
	{
		return false;
	}

	return swap_in(page, frame->kva);
}

/* 가상 주소를 hashed index로 변환하는 해시 함수 */
unsigned page_hash(const struct hash_elem *p_, void *aux UNUSED)
{
	const struct page *p = hash_entry(p_, struct page, hash_elem); // 해당 페이지가 들어있는 해시 테이블의 시작 주소를 가져온다. 우리는 hash_elem을 알고 있다.
	return hash_bytes(&p->va, sizeof p->va);											 // 가상 주소에 대한 해시 값을 반환
}

/* 해시 테이블 내 두 페이지 요소에 대해 페이지 주소값을 비교 */
bool page_less(const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED)
{
	const struct page *a = hash_entry(a_, struct page, hash_elem);
	const struct page *b = hash_entry(b_, struct page, hash_elem);

	return a->va < b->va;
}

/* Initialize new supplemental page table */
/* spt 초기화 */
void supplemental_page_table_init(struct supplemental_page_table *spt UNUSED)
{
	// 해시를 이용하여 해시 값을 계산하고 비교함수 less를 이용해 해시 테이블 내 요소를 비교하는 테이블을 만든다.
	// 가상 주소를 해시 값으로 변환하고 이를 기준으로 정렬함(같은 해시값에서)으로써, 페이지의 검색과 관리가 더 효율적으로 이루어질 수 있다.
	hash_init(&spt->spt_hash, page_hash, page_less, NULL);
}

/* Copy supplemental page table from src to dst */
bool supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED,
																	struct supplemental_page_table *src UNUSED)
{
}

/* Free the resource hold by the supplemental page table */
void supplemental_page_table_kill(struct supplemental_page_table *spt UNUSED)
{
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
}
