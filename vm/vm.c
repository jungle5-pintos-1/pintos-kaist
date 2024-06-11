/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "lib/kernel/hash.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"
#include "vm/uninit.h"
#include "lib/string.h"
#include "userprog/process.h"
#include "threads/thread.h"

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

	// Check wheter the upage is already occupied or not.
	if (spt_find_page (spt, upage) == NULL) {	
		struct page *p = (struct page *)malloc(sizeof(struct page));

		// 초기화 함수 설정 (함수 포인터로 전달)
		bool (*page_initializer)(struct page *, enum vm_type, void *);

		switch (VM_TYPE(type)) {
			case VM_ANON:
				page_initializer = anon_initializer;
				break;

			case VM_FILE:
				page_initializer = file_backed_initializer;
				break; 
		}

		// uninit_new() 함수를 통해 VM_UNUNIT type PAGE 생성
		// p: 할당한 page, upage: va, init: 초기화 함수, type: vm_type, aux: init에 필요한 보조값, page_initializer: p를 type에 맞게 초기화하는 함수
		uninit_new(p, upage, init, type, aux, page_initializer);
		
		p->writable = writable;

		// Insert the page into the spt.
		return spt_insert_page(spt, p);
	}
err:
	return false;
}

/* Find VA from SPT and return PAGE. On error, return NULL. (SPT에서 VA에 해당하는 PAGE 찾기)*/
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page *page = NULL;
	
	// PAGE 공간을 할당받아 VA와 일치하는 hash_elem을 찾는다.
	page = (struct page *)malloc(sizeof(struct page));
	struct hash_elem *e;

	page->va = pg_round_down(va); 
	e = hash_find(&spt->spt_hash, &page->hash_elem);
	free(page);

	if (e != NULL) {
		return hash_entry(e, struct page, hash_elem);
	}
	
	else {
		return NULL;
	}
}

/* Insert PAGE into spt with validation. (해당 PAGE가 SPT 안에 존재하는지 검사하여 존재하지 않는다면 삽입한다.) */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED, struct page *page UNUSED) {
	int succ = false;
	
	// 삽입을 성공했을 때 (hash_insert에서 검증 과정을 거치고, 성공하면 NULL을 return)
	if (hash_insert(&spt->spt_hash, &page->hash_elem) == NULL) {
		succ = true;
	}

	return succ;
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
	struct frame *frame = NULL;
	
	// FRAME 할당
	void *kva = palloc_get_page(PAL_USER);

	// todo: FRAME 할당이 실패했을 경우, swap out!
	if (kva == NULL) {
		PANIC("todo");
	}

	// 할당된 FRAME 초기화
	frame = (struct frame *)malloc(sizeof(struct frame));
	frame->kva = kva;
	frame->page = NULL;

	// FRAME -> FRAME TABLE에 넣기
	lock_acquire(&frame_table_lock);
	list_push_back(&frame_table, &frame->frame_elem);
	lock_release(&frame_table_lock);

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);

	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
	vm_alloc_page(VM_ANON | VM_MARKER_0, pg_round_down(addr), 1);
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

	if (addr == NULL) {
		return false;
	}

	/* User Stack의 주소가 아닐 때 */
	if (is_kernel_vaddr(addr)) {
		return false;
	}

	/* 접근한 메모리의 Physical Page가 존재하지 않은 경우 */
	if (not_present) {
		void *rsp = f->rsp; 

		// bool user | true: access by user, false: access by kernel.
		if (!user) {
			rsp = thread_current()->rsp;
		}

		/* Stack Growth로 처리할 수 있는 Page fault일 경우 */
		if (USER_STACK - (1 << 20) <= rsp - 8 && rsp - 8 == addr && addr <= USER_STACK) {
			vm_stack_growth(addr);
		}

		else if (USER_STACK - (1 << 20) <= rsp && rsp <= addr && addr <= USER_STACK) {
			vm_stack_growth(addr);
		}

		page = spt_find_page(spt, addr);

		if (page == NULL) {
			return false;
		}

		// bool write | true: access was write, false: access was read.
		if (write == 1 && page->writable == 0) {
			return false;
		}

		return vm_do_claim_page (page);
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

/* Claim the page that allocate on VA. (VA에 해당하는 PAGE 찾아 FRAME - PAGE 간의 mapping 요청하기)*/
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = NULL;
	struct thread *current = thread_current();

	page = spt_find_page(&current->spt, va);

	if (page == NULL) {
		return false;
	}

	return vm_do_claim_page(page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* Insert page table entry to map page's VA to frame's PA. */
	struct thread *current = thread_current();
	pml4_set_page(current->pml4, page->va, frame->kva, page->writable);

	return swap_in(page, frame->kva);
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	hash_init(&spt->spt_hash, page_hash, page_less, NULL);
}

/* Copy supplemental page table from src to dst (부모 프로세스의 SPT를 자식 프로세스의 DST로 복사) */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
	struct hash_iterator i;
	hash_first(&i, &src->spt_hash);
	
	// 부모 프로세스의 SPT 순회
	while (hash_next(&i)) {
		struct page *src_page = hash_entry(hash_cur(&i), struct page, hash_elem);
		enum vm_type type = src_page->operations->type;
		void *upage = src_page->va;
		bool writable = src_page->writable;

		/* (1) 부모 프로세스가 가진 PAGE의 type이 VM_UNINIT일 때 */
		if (type == VM_UNINIT) {
			vm_initializer *init = src_page->uninit.init;
			void *aux = src_page->uninit.aux;
			vm_alloc_page_with_initializer(VM_ANON, upage, writable, init, aux);
			continue;
		}

		/* (2) */
		if (type == VM_FILE) {
			struct lazy_load_arg *file_aux = malloc(sizeof(struct lazy_load_arg));
			
			file_aux->file = src_page->file.file;
			file_aux->ofs = src_page->file.ofs;
			file_aux->read_bytes = src_page->file.read_bytes;
			file_aux->zero_bytes = src_page->file.zero_bytes;

			if (!vm_alloc_page_with_initializer(type, upage, writable, NULL, file_aux)) {
				return false;
			}

			struct page *file_page = spt_find_page(dst, upage);

			file_backed_initializer(file_page, type, NULL);
			file_page->frame = src_page->frame;
			pml4_set_page(thread_current()->pml4, file_page->va, src_page->frame->kva, src_page->writable);
			continue;
		}

		/* (3) 부모 프로세스가 가진 PAGE의 type이 VM_UNINIT이 아닐 때 */
		else {
			// 현재 만드는 PAGE는 기다리지 않고 바로 내용을 넣어줄 것이므로 init, aux을 NULL로 정해주기
			if (!vm_alloc_page(type, upage, writable)) {
				return false;
			}
		}

		if (!vm_claim_page(upage)) {
			return false;
		}

		struct page *dst_page = spt_find_page(dst, upage);
		memcpy(dst_page->frame->kva, src_page->frame->kva, PGSIZE);
	}

	return true;
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* Destroy all the supplemental_page_table hold by thread and
	 * writeback all the modified contents to the storage. */
	hash_clear(&spt->spt_hash, hash_page_destory);
}

void hash_page_destory(struct hash_elem *e, void *aux) {
	struct page *p = hash_entry(e, struct page, hash_elem);
	destroy(p);
	free(p);
}

/* Returns a hash value (index) for page p */
unsigned 
page_hash(const struct hash_elem *e, void *aux UNUSED) {
	const struct page *p = hash_entry(e, struct page, hash_elem);
	return hash_bytes(&p->va, sizeof p->va);
}

/* Returns true if page a preceds page b */
bool
page_less(const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED) {
	const struct page *a = hash_entry(a_, struct page, hash_elem);
	const struct page *b = hash_entry(b_, struct page, hash_elem);

	return a->va < b->va;
}