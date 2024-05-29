#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#include "threads/synch.h"
#ifdef VM
#include "vm/vm.h"
#endif

/* States in a thread's life cycle. */
enum thread_status
{
	THREAD_RUNNING, /* Running thread. */
	THREAD_READY,		/* Not running but ready to run. */
	THREAD_BLOCKED, /* Waiting for an event to trigger. */
	THREAD_DYING		/* About to be destroyed. */
};

/* Thread identifier type.
	 You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) - 1) /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0			 /* Lowest priority. */
#define PRI_DEFAULT 31 /* Default priority. */
#define PRI_MAX 63		 /* Highest priority. */

/* Advenced Scheduler */
#define NICE_DEFAULT 0
#define RECENT_CPU_DEFAULT 0
#define LOAD_AVG_DEFAULT 0

/* Project 2 */
#define FDT_PAGES 2
#define FDT_COUNT_LIMIT 128

/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
struct thread
{
	/* Owned by thread.c. */
	tid_t tid;								 /* Thread identifier. */
	enum thread_status status; /* Thread state. */
	char name[16];						 /* Name (for debugging purposes). */
	int priority;							 /* Priority. */

	/* Shared between thread.c and synch.c. */
	struct list_elem elem; /* List element. */

	int64_t wakeup_tick;

	int init_priority;							// donation이 종료될 때 기존의 priority로 돌아오기 위한 필드
	struct lock *wait_on_lock;			// 스레드가 요청했지만 다른 스레드가 점유하고 있어서 획득하지 못하고 기다리는 lock
	struct list donations;					// lock을 요청하면서 priority를 기부한 스레드들
	struct list_elem donation_elem; // donations에 들어가기위한 elem

	// advanced scheduler(mlfqs)
	int nice;
	int recent_cpu;
	struct list_elem all_elem;
	struct list all_list; // 생성되는 모든 리스트

#ifdef USERPROG
	/* Owned by userprog/process.c. */
	uint64_t *pml4; /* Page map level 4 */
#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
#endif

	/* Owned by thread.c. */
	struct intr_frame tf; /* Information for switching */
	unsigned magic;				/* Detects stack overflow. */

	/* Project2 : User programs - system call - */
	int exit_status;										 // exit(), wait() 구현 때 사용
	struct file **file_descriptor_table; // FDT
	int fdidx;													 // fd index

	struct intr_frame parent_if; // 부모 프로세스의 유저 스택 정보를 담아야 한다.
	struct list child_list;			 // 자식 프로세스 리스트
	struct list_elem child_elem; // 자식 리스트 요소

	struct semaphore load_sema;
	struct semaphore exit_sema;
	struct semaphore wait_sema;
};

/* If false (default), use round-robin scheduler.
	 If true, use multi-level feedback queue scheduler.
	 Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init(void);
void thread_start(void);

void thread_tick(void);
void thread_print_stats(void);

typedef void thread_func(void *aux);
tid_t thread_create(const char *name, int priority, thread_func *, void *);

void thread_block(void);
void thread_unblock(struct thread *);

struct thread *thread_current(void);
tid_t thread_tid(void);
const char *thread_name(void);

void thread_exit(void) NO_RETURN;
void thread_yield(void);

int thread_get_priority(void);
void thread_set_priority(int);

int thread_get_nice(void);
void thread_set_nice(int);
int thread_get_recent_cpu(void);
int thread_get_load_avg(void);

void do_iret(struct intr_frame *tf);

void thread_sleep(int64_t ticks);

// 우선순위 비교
bool cmp_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);
void preempt_priority(void); // ready에 있는 스레드가 현재 실행되는 스레드 보다 우선순위가 높으면 교체

bool cmp_d_priority(const struct list_elem *a, const struct list_elem *b, void *aux);
void donate_priority(void);
void remove_donor(struct lock *lock);
void update_priority_before_donations(void);

void mlfqs_calculate_priority(struct thread *t);
void mlfqs_calculate_recent_cpu(struct thread *t);
void mlfqs_calculate_load_avg(void);
void mlfqs_increment_recent_cpu(void);
void mlfqs_recalculate_recent_cpu(void);
void mlfqs_recalculate_priority(void);

#endif /* threads/thread.h */
