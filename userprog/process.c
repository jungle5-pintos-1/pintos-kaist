#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#include "threads/synch.h"
#include "userprog/syscall.h"
#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup(void);
static bool load(const char *file_name, struct intr_frame *if_);
static void initd(void *f_name);
static void __do_fork(void *);

void argument_stack(char **parse, int count, void **rsp);
int process_add_file(struct file *file);
struct file *process_get_file(int fd);
void process_close_file(int fd);

struct thread *get_child_process(int pid);

/* General process initializer for initd and other process. */
static void
process_init(void)
{
	struct thread *current = thread_current();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE.
 * 이 함수는 FILE_NAME으로 지정된 프로그램을 로드하여 실행하는 새로운 스레드를 생성합니다 */
tid_t process_create_initd(const char *file_name)
{
	char *fn_copy; // 파일 이름의 복사본을 저장할 포인터
	tid_t tid;		 // 생성될 스레드의 ID를 저장할 변수

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load().
	 * 메모리 할당 및 검증 */
	fn_copy = palloc_get_page(0); // 파일 이름을 저장하기 위한 메모리 페이지 할당
	if (fn_copy == NULL)					// 실패하면 NULL 반환
		return TID_ERROR;						// 에러 상황 알림

	// 파일 이름 복사, 페이지 크기 만큼으로 제한하여 버퍼 오버플로 방지
	// 이 과정은 파일 이름을 스레드 생성 과정 중 안전하게 참조할 수 있도록 한다
	strlcpy(fn_copy, file_name, PGSIZE); // 복사를 해둬서 initd에서 사용가능

	// Argument Passing ~
	char *save_ptr;
	strtok_r(file_name, " ", &save_ptr); // 복사 후에 자른다
	// ~ Argument Passing

	/* Create a new thread to execute FILE_NAME. */
	// file_name을 이름으로 하고 기본 우선순위 PRI_DEFAULT와 스레드 함수 initd
	// 그리고 fn_copy를 인자로 받아 새로운 스레드 생성
	tid = thread_create(file_name, PRI_DEFAULT, initd, fn_copy);
	if (tid == TID_ERROR)				 // 생성 실패하면
		palloc_free_page(fn_copy); // 할당된 메모리 페이지 fn_copy 해제
	return tid;									 // 생성된 스레드 ID 반환, 실패하면 TID_ERROR 반환
}

/* A thread function that launches first user process. */
// 첫 번째 사용자 프로세스를 시작하는 스레드 함수, 주어진 파일 이름으로 프로그램 로드, 실행
static void
initd(void *f_name)
{					// 실행할 파일의 이름을 나타내는 포인터
#ifdef VM // VM이 정의되어 있을 경우, 현재 스레드 보조 페이지 테이블 초기화
	supplemental_page_table_init(&thread_current()->spt);
#endif

	process_init(); // 프로세스 초기화

	if (process_exec(f_name) < 0) // 프로세스 로드하고 실행, 성공하면 0이상 값
		PANIC("Fail to launch initd\n");
	NOT_REACHED(); // 여기에 도착하면 프로그래밍 오류(process_exec에서 프로세스 시작되어 initd는 더이상 실행되면 안된다)
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
tid_t process_fork(const char *name, struct intr_frame *if_ UNUSED)
{
	/* Clone current thread to new thread.*/
	// 현재 스레드의 parent_if에 복제해야하는 if_를 복사한다
	struct thread *cur = thread_current();
	memcpy(&cur->parent_if, if_, sizeof(struct intr_frame));

	// 현재 스레드를 fork한 new 스레드를 생성
	tid_t pid = thread_create(name,
														PRI_DEFAULT, __do_fork, thread_current()); // __do_fork 는 부모 프로세스의 내용을 자식 프로세스로 복사하는 함수
	if (pid == TID_ERROR)
	{
		return TID_ERROR;
	}

	// 자식이 로드될 때까지 대기하기 위해서 방금 생성한 자식 스레드를 찾는다.
	// load가 완료될 때까지 부모를 재워야 하기 때문에 semaphore 이용
	struct thread *child = get_child_process(pid);

	// 현재 스레드는 생성만 완료된 상태
	// ready_list에서 실행될 때 __do_fork가 실행된다
	// 로드가 완료될 때 까지 부모 대기
	sema_down(&child->load_sema);

	return pid; // 자식프로세스의 pid 반환
}

/* pid를 인자로 받아 자식 스레드를 반환하는 함수 */
struct thread *get_child_process(int pid)
{
	// 자식 리스트에 접근하여 프로세스 디스크립터 검색
	struct thread *cur = thread_current();
	struct list *child_list = &cur->child_list;
	for (struct list_elem *e = list_begin(child_list); e != list_end(child_list); e = list_next(e))
	{
		struct thread *t = list_entry(e, struct thread, child_elem);
		// 해당 pid가 존재하면 프로세스 디스크립터(프로세스 포인터) 반환
		if (t->tid == pid)
		{
			return t;
		}
	}
	return NULL; // 없으면 NULL 반환
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
static bool
duplicate_pte(uint64_t *pte, void *va, void *aux)
{
	struct thread *current = thread_current();
	struct thread *parent = (struct thread *)aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. TODO: If the parent_page is kernel page, then return immediately. */
	if (is_kernel_vaddr(va))
	{
		return true;
	}

	/* 2. Resolve VA from the parent's page map level 4. */
	parent_page = pml4_get_page(parent->pml4, va);
	if (parent_page == NULL)
	{
		return false;
	}

	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */
	newpage = palloc_get_page(PAL_USER | PAL_ZERO); // 비트연산자 OR
	if (newpage == NULL)
	{
		return false;
	}

	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */
	memcpy(newpage, parent_page, PGSIZE);
	writable = is_writable(pte);

	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */
	if (!pml4_set_page(current->pml4, va, newpage, writable))
	{
		/* 6. TODO: if fail to insert page, do error handling. */
		return false;
	}
	return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
// Userland context : 유저 모드에서 실행 중인 프로세스의 상태와 관련된 정보
static void
__do_fork(void *aux)
{
	struct intr_frame if_;
	struct thread *parent = (struct thread *)aux;
	struct thread *current = thread_current();
	/* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
	struct intr_frame *parent_if = &parent->parent_if;
	bool succ = true;

	/* 1. Read the cpu context to local stack. */
	memcpy(&if_, parent_if, sizeof(struct intr_frame));
	if_.R.rax = 0; // 자식 프로세스의 리턴값은 0

	/* 2. Duplicate PT */
	current->pml4 = pml4_create();
	if (current->pml4 == NULL)
		goto error;

	process_activate(current);
#ifdef VM
	supplemental_page_table_init(&current->spt);
	if (!supplemental_page_table_copy(&current->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each(parent->pml4, duplicate_pte, parent))
		goto error;
#endif

	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent.*/
	// 자식 프로세스의 FDT는 부모의 FDT와 동일하게 해줘야 한다.
	current->file_descriptor_table[0] = parent->file_descriptor_table[0];
	current->file_descriptor_table[1] = parent->file_descriptor_table[1];
	for (int i = 2; i < FDT_COUNT_LIMIT; i++)
	{
		struct file *file = parent->file_descriptor_table[i];
		if (file == NULL)
			continue;
		current->file_descriptor_table[i] = file_duplicate(file);
	}
	current->fdidx = parent->fdidx;

	// 기다리고 있던 부모 대기 해제
	sema_up(&current->load_sema);
	process_init();

	/* Finally, switch to the newly created process. */
	if (succ)
		do_iret(&if_);
error:
	sema_up(&current->load_sema);
	exit(TID_ERROR);
	// thread_exit();
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
// 주어진 파일 이름을 사용하여 새로운 실행 컨텍스트로 전환하는 기능
int process_exec(void *f_name)
{
	char *file_name = f_name;
	bool success; // 프로그램 로드 성공 여부 저장하기 위한 변수

	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	struct intr_frame _if;								// 구조체의 실행 상태
	_if.ds = _if.es = _if.ss = SEL_UDSEG; // 사용자 데이터 세그먼트
	_if.cs = SEL_UCSEG;										// 사용자 코드 세그먼트
	_if.eflags = FLAG_IF | FLAG_MBS;			// 인터럽트 활성화 플래그, 필수 플래그 (사용자 모드에서 안전한 실행 보장)

	/* We first kill the current context */
	// 현재 컨텍스트 정리
	process_cleanup(); // 새 프로그램을 로드하기 전에 현재 프로세스의 메모리 할당 등을 해제하여 상태를 초기화

	char *parse[64]; // command line의 길이가 128로 제한 -> 인자와 공백을 합치면 2바이트, 최대 64개의 인자 구분 가능
	char *save_ptr;
	int count = 0;

	char *token = strtok_r(file_name, " ", &save_ptr); // 파일 이름만 자른다
	while (token != NULL)
	{
		parse[count++] = token; // parse 배열에 인자를 담는다
		token = strtok_r(NULL, " ", &save_ptr);
	}

	/* And then load the binary */
	// 이진 파일 로드
	// success = load(file_name, &_if);
	lock_acquire(&filesys_lock);
	success = load(file_name, &_if); // 로드 함수를 호출하여 파일 이름에 해당하는 프로그램을 메모리에 로드
	lock_release(&filesys_lock);
	argument_stack(parse, count, &_if.rsp); // 스택에 파일 이름도 넣어야 한다
	_if.R.rdi = count;											// rdi에는 argc인 인자 개수
	_if.R.rsi = (char *)_if.rsp + 8;				// rsi에는 인자의 시작 주소

	// hex_dump(_if.rsp, _if.rsp, USER_STACK - (uint64_t)_if.rsp, true); // user stack을 16진수로 프린트/

	/* If load failed, quit. */
	// 실패하면 메모리 페이지 해제하고 -1 반환, exit() 추가?
	palloc_free_page(file_name);
	if (!success)
		return -1;

	/* Start switched process. */
	// 저장된 인터럽트 프레임을 사용해 새로 로드된 사용자 프로그램으로
	// 컨텍스트 전환
	do_iret(&_if); // 프로세스의 실행을 시작
	NOT_REACHED();
}
/* process.c */

void argument_stack(char **parse, int count, void **rsp)
{
	// 프로그램 이름과 인자 문자열 푸시 (역순으로)
	for (int i = count - 1; i >= 0; i--)
	{
		for (int j = strlen(parse[i]); j >= 0; j--) // '\0'이 있으므로 strlen(parse[i]) 부터 시작
		{
			(*rsp)--;											// 스택 주소 감소 (1바이트)
			**(char **)rsp = parse[i][j]; // 주소에 문자 저장
		}
		parse[i] = *(char **)rsp; // parse[i]에 현재 rsp의 값(각 인자의 시작 주소) 저장해둠
	}

	// 정렬 패딩 푸시 (8 바이트 정렬로 맞추기 위해)
	int padding = (int)*rsp % 8;
	for (int i = 0; i < padding; i++)
	{
		(*rsp)--;
		**(uint8_t **)rsp = 0; // rsp 직전까지 값 채움
	}

	// 인자 문자열 종료를 나타내는 0푸시
	(*rsp) -= 8;				 // 포인터 이므로 8바이트
	**(char ***)rsp = 0; // char* 타입의 0 추가

	// 각 인자 문자열의 주소 푸시 (역순)
	for (int i = count - 1; i >= 0; i--)
	{
		(*rsp) -= 8;								// 다음 주소로 이동
		**(char ***)rsp = parse[i]; // char * 타입 주소 추가
	}

	// 리턴 주소 푸시
	(*rsp) -= 8;
	**(void ***)rsp = 0; // void* 타입 0 추가 (fake return address, 프로세스 생성이라 반환 주소가 없다)
}
/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
int process_wait(tid_t child_tid UNUSED)
{
	/* XXX: Hint) The pintos exit if process_wait (initd), we recommend you
	 * XXX:       to add infinite loop here before
	 * XXX:       implementing the process_wait. */
	/*1) get_child_process 함수를 만들어서 사용한다. 인자로 받은 tid를 갖는 자식이 없는 경우에는 -1을 반환하고 종료한다.

		2) 찾은 자식이 sema_up 해줄때까지 (종료될 때까지) 대기한다.

		3) 자식에게서 종료 signal이 도착하면 자식 리스트에서 해당 자식을 제거한다.

		4) 자식이 완전히 종료되어도 괜찮은지 대기하고 있으므로, sema_up으로 signal을 보내 완전히 종료되게 해주고,

		5) 자식의 exit_status를 반환하고 함수를 종료한다.*/

	// for (int i = 0; i < 1000000000; i++);

	struct thread *child = get_child_process(child_tid);
	if (child == NULL) // 1) 자식이 아니면 -1 반환
	{
		return -1;
	}

	// 2) 자식이 종료될 때 까지 대기
	sema_down(&child->wait_sema);

	// 3) 자식이 종료됨을 알리는 wait_sema를 받으면 자식 리스트에서 제거
	list_remove(&child->child_elem);

	// 4) 자식이 완전히 종료되고 스케줄링이 이어질 수 있도록 자식에게 signal보낸다.
	sema_up(&child->exit_sema);

	return child->exit_status; // 5) 자식의 exit_status 반환
}

/* Exit the process. This function is called by thread_exit (). */
// 실행중인 스레드 종료, exec에 추가해야될듯 ?
void process_exit(void)
{
	struct thread *curr = thread_current();
	/* TODO: Your code goes here.
	 * TODO: Implement process termination message (see
	 * TODO: project2/process_termination.html).
	 * TODO: We recommend you to implement process resource cleanup here. */
	/*1) FDT의 모든 파일을 닫고 메모리도 반환한다.

		2) 현재 실행 중인 파일도 닫는다.

		3) 자식이 종료되기를 기다리고 있는 (wait) 부모에게 sema_up으로 signal을 보낸다.

		4) 부모가 wait을 마무리하고 나서 signal을 보내줄 때까지 대기한다.*/

	// 1) FDT의 모든 파일을 닫고 메모리 반환
	for (int i = 0; i < FDT_COUNT_LIMIT; i++)
	{
		close(i);
	}
	// palloc_free_page(curr->file_descriptor_table); // 한 번에 하나의 메모리 페이지만 해제 -> FDT가 여러 페이지를 사용할 때 적절하게 해제가 안될 수도 있다
	palloc_free_multiple(curr->file_descriptor_table, FDT_PAGES); // 여러 페이지 동시에 해제 -> 모든 관련 페이지를 한 번에 해제 -> 메모리 누수 방지 (mulit-oom), get이 multiple로 받아서 그런듯

	// 2) 실행 중인 파일도 닫는다 - 아직 구현 미진행
	file_close(curr->running); // rox
	process_cleanup();

	// 3) 자식이 종료 될때 까지 대기하고 있는 부모에게 시그널
	sema_up(&curr->wait_sema);

	// 4) 부모의 시그널을 기다리고 대기가 풀리면 do_schedule 진행
	sema_down(&curr->exit_sema);
}

/* Free the current process's resources. */
static void
process_cleanup(void)
{
	struct thread *curr = thread_current();

#ifdef VM
	supplemental_page_table_kill(&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL)
	{
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		curr->pml4 = NULL;
		pml4_activate(NULL);
		pml4_destroy(pml4);
	}
}

/* Project 2 - system-call */
/* 파일 객체에 대한 파일 디스크립터를 생성하는 함수 */
int process_add_file(struct file *file)
{
	struct thread *curr = thread_current();
	struct file **fdt = curr->file_descriptor_table;

	// FDT 한계와 같지 않을때 까지 fdidx값을 증가시킨다
	while (curr->fdidx < FDT_COUNT_LIMIT && fdt[curr->fdidx])
	{
		curr->fdidx++;
	}
	if (curr->fdidx >= FDT_COUNT_LIMIT)
	{
		return -1;
	}
	fdt[curr->fdidx] = file; // 빈자리에 f를 넣고
	return curr->fdidx;			 // fd 반환
}

/* 파일 객체를 검색하는 함수 */
struct file *process_get_file(int fd)
{
	struct thread *curr = thread_current();
	struct file **fdt = curr->file_descriptor_table;

	if (fd < 2 || fd >= FDT_COUNT_LIMIT) // fd가 2보다 작거나 한계만큼 크면 NULL
	{
		return NULL;
	}
	return fdt[fd]; // 파일 디스크립터에 해당하는 파일 객체를 리턴
}

// 파일 디스크립터 테이블에서 파일 객체를 제거하는 함수
void process_close_file(int fd)
{
	struct thread *curr = thread_current();
	struct file **fdt = curr->file_descriptor_table;
	if (fd < 2 || fd >= FDT_COUNT_LIMIT)
		return NULL;
	fdt[fd] = NULL; // fd에 해당하는 index에 NULL값
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void process_activate(struct thread *next)
{
	/* Activate thread's page tables. */
	pml4_activate(next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update(next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL 0						/* Ignore. */
#define PT_LOAD 1						/* Loadable segment. */
#define PT_DYNAMIC 2				/* Dynamic linking info. */
#define PT_INTERP 3					/* Name of dynamic loader. */
#define PT_NOTE 4						/* Auxiliary info. */
#define PT_SHLIB 5					/* Reserved. */
#define PT_PHDR 6						/* Program header table. */
#define PT_STACK 0x6474e551 /* Stack segment. */

#define PF_X 1 /* Executable. */
#define PF_W 2 /* Writable. */
#define PF_R 4 /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr
{
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR
{
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack(struct intr_frame *if_);
static bool validate_segment(const struct Phdr *, struct file *);
static bool load_segment(struct file *file, off_t ofs, uint8_t *upage,
												 uint32_t read_bytes, uint32_t zero_bytes,
												 bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise.
 * ELF 실행 파일을 현재 스레드의 메모리에 로드하고,
 * 해당 실행 파일의 엔트리 포인트(프로그램이 실행될 때 처음으로 실행을 시작하는 코드의 주소)와 초기 스택 포인터를
 * intr_frame 구조체에 저장하는 과정 수행 */
static bool
load(const char *file_name, struct intr_frame *if_)
{
	struct thread *t = thread_current(); // 현재 스레드 참조
	struct ELF ehdr;										 // ELF 헤더 정보
	struct file *file = NULL;						 // 실행 파일 참조
	off_t file_ofs;											 // 파일 내 오프셋을 저장
	bool success = false;								 // 작업 성공 여부
	int i;

	/* Allocate and activate page directory. */
	t->pml4 = pml4_create(); // 페이지 맵 레벨 4 생성 (가상 메모리 관리)
	if (t->pml4 == NULL)
		goto done;												// 실패하면 done 으로
	process_activate(thread_current()); // 현재 스레드의 페이지 테이블 활성화

	/* Open executable file. */
	file = filesys_open(file_name); // 실행 파일 열기
	if (file == NULL)
	{
		printf("load: %s: open failed\n", file_name);
		goto done;
	}

	/* Read and verify executable header. */
	if (file_read(file, &ehdr, sizeof ehdr) != sizeof ehdr // file_read 통해 ELF 헤더를 읽음
			|| memcmp(ehdr.e_ident, "\177ELF\2\1\1", 7)				 // 여러 조건 검사
			|| ehdr.e_type != 2 || ehdr.e_machine != 0x3E			 // amd64
			|| ehdr.e_version != 1 || ehdr.e_phentsize != sizeof(struct Phdr) || ehdr.e_phnum > 1024)
	{
		printf("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++)
	{ // 헤더를 순차적으로 읽는다
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length(file))
			goto done;
		file_seek(file, file_ofs);

		if (file_read(file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type)
		{
		case PT_NULL:
		case PT_NOTE:
		case PT_PHDR:
		case PT_STACK:
		default:
			/* Ignore this segment. */
			break;
		case PT_DYNAMIC:
		case PT_INTERP:
		case PT_SHLIB:
			goto done;
		case PT_LOAD: // PT_LOAD 타입 세그먼트는 load_segment() 함수를 통해 메모리에 로드
			if (validate_segment(&phdr, file))
			{
				bool writable = (phdr.p_flags & PF_W) != 0;
				uint64_t file_page = phdr.p_offset & ~PGMASK;
				uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
				uint64_t page_offset = phdr.p_vaddr & PGMASK;
				uint32_t read_bytes, zero_bytes;
				if (phdr.p_filesz > 0)
				{
					/* Normal segment.
					 * Read initial part from disk and zero the rest. */
					read_bytes = page_offset + phdr.p_filesz;
					zero_bytes = (ROUND_UP(page_offset + phdr.p_memsz, PGSIZE) - read_bytes);
				}
				else
				{
					/* Entirely zero.
					 * Don't read anything from disk. */
					read_bytes = 0;
					zero_bytes = ROUND_UP(page_offset + phdr.p_memsz, PGSIZE);
				}
				if (!load_segment(file, file_page, (void *)mem_page,
													read_bytes, zero_bytes, writable))
					goto done;
			}
			else
				goto done;
			break;
		}
	}

	/* rox */
	t->running = file;		 // 스레드가 삭제될 때 파일을 닫을 수 있게 구조체에 파일을 저장
	file_deny_write(file); // 현재 실행중인 파일은 수정할 수 없게 막는다.

	/* Set up stack. */
	if (!setup_stack(if_)) // 스택 초기화
		goto done;

	/* Start address. */
	if_->rip = ehdr.e_entry; // if_->rip에 실행 파일의 엔트리 포인트 설정

	/* TODO: Your code goes here.
	 * TODO: Implement argument passing (see project2/argument_passing.html). */

	success = true;

done:
	/* We arrive here whether the load is successful or not. */
	// file_close(file);
	return success;
}

/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment(const struct Phdr *phdr, struct file *file)
{
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t)file_length(file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
		 user address space range. */
	if (!is_user_vaddr((void *)phdr->p_vaddr))
		return false;
	if (!is_user_vaddr((void *)(phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
		 address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
		 Not only is it a bad idea to map page 0, but if we allowed
		 it then user code that passed a null pointer to system calls
		 could quite likely panic the kernel by way of null pointer
		 assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page(void *upage, void *kpage, bool writable);

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment(struct file *file, off_t ofs, uint8_t *upage,
						 uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
	ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT(pg_ofs(upage) == 0);
	ASSERT(ofs % PGSIZE == 0);

	file_seek(file, ofs);
	while (read_bytes > 0 || zero_bytes > 0)
	{
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page(PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read(file, kpage, page_read_bytes) != (int)page_read_bytes)
		{
			palloc_free_page(kpage);
			return false;
		}
		memset(kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page(upage, kpage, writable))
		{
			printf("fail\n");
			palloc_free_page(kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
static bool
setup_stack(struct intr_frame *if_)
{
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page(PAL_USER | PAL_ZERO);
	if (kpage != NULL)
	{
		success = install_page(((uint8_t *)USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page(kpage);
	}
	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool
install_page(void *upage, void *kpage, bool writable)
{
	struct thread *t = thread_current();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page(t->pml4, upage) == NULL && pml4_set_page(t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

// 실행 파일의 내용을 페이지로 로드하는 함수
// 첫 번째 page fault가 발생할 때 호출된다.
static bool
lazy_load_segment(struct page *page, void *aux)
{
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
	struct container *lazy_load_arg = (struct container *)aux; // 인자로 받아온 lazy_load_arg 컨테이너
	struct file *file = lazy_load_arg->file;
	off_t ofs = lazy_load_arg->ofs;
	size_t page_read_bytes = lazy_load_arg->page_read_bytes;
	size_t page_zero_bytes = lazy_load_arg->page_zero_bytes;

	// 1) 파일의 position을 ofs으로 지정
	file_seek(file, ofs);

	// 2) 파일을 read_bytes만큼 물리 프레임에 읽어 들인다.(로딩)
	if (file_read(file, page->frame->kva, page_read_bytes) != (int)page_read_bytes) // 물리 프레임에서 읽은 바이트 수가 요청된 바이트 수와 동일한지 확인
	{
		palloc_free_page(page->frame->kva);
		return false;
	}

	// 3) 다 읽은 지점부터 zero_bytes만큼 0으로 채운다.
	memset(page->frame->kva + page_read_bytes, 0, page_zero_bytes);
	return true;
}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
// 파일의 내용을 upage에 로드하는 함수
// 프로세스가 실행될 때 실행 파일을 현재 스레드로 로드하는 함수인 load 함수에서 호출된다.
static bool
load_segment(struct file *file, off_t ofs, uint8_t *upage,
						 uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
	// read_bytes + zero_bytes 가 페이지 크기의 배수인지 확인
	ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
	// upage가 정렬되어 있는지 확인
	ASSERT(pg_ofs(upage) == 0);
	// offset이 페이지 정렬되어 있는지 확인
	ASSERT(ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0)
	{
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		// void *aux = NULL;
		struct container *lazy_load_arg = (struct container *)malloc(sizeof(struct container));
		lazy_load_arg->file = file;												// 내용이 담긴 파일 객체
		lazy_load_arg->ofs = ofs;													// 이 페이지에서 읽기 시작한 위치
		lazy_load_arg->page_read_bytes = page_read_bytes; // 이 페이지에서 읽어야 하는 바이트 수
		lazy_load_arg->page_zero_bytes = page_zero_bytes; // 이 페이지에서 read_bytes만큼 읽고 공간이 남아 0으로 채워야 하는 바이트 수

		// 페이지 폴트가 발생했을 때 데이터를 로드하기 위한 준비(각 페이지에 필요한 메타 데이터 등을 설정)
		// 후에 페이지 폴트가 발생했을 때 로드된다.
		if (!vm_alloc_page_with_initializer(VM_ANON, upage,
																				writable, lazy_load_segment, lazy_load_arg))
			return false;

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
		ofs += page_read_bytes;
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
// load에서 호출된다.

static bool
setup_stack(struct intr_frame *if_)
{
	bool success = false;
	// 스택은 아래로 성장하므로, 스택의 시작점인 USER_STACK에서 PGSIZE만큼 아래로 내린 지점에서 페이지 생성
	void *stack_bottom = (void *)(((uint8_t *)USER_STACK) - PGSIZE);

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */

	// 1) stack_bottom에 페이지 하나 할당받는다.
	// VM_MARKER_0 : 스택이 저장된 메모리 페이징을 식별하기 위해 추가
	// writable : argument_stack()에서 값을 넣어야 하니 True
	if (vm_alloc_page(VM_ANON | VM_MARKER_0, stack_bottom, 1))
	{
		// 2) 할당 받은 페이지에 물리 프레임 매핑
		success = vm_claim_page(stack_bottom);
		if (success)
		{
			// 3) 성공하면 rsp를 변경(argument_stack에서 이 위치부터 인자를 push한다.)
			if_->rsp = USER_STACK;
		}
	}

	return success;
}
#endif /* VM */
