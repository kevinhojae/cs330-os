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
enum thread_status {
	THREAD_RUNNING,     /* Running thread. */
	THREAD_READY,       /* Not running but ready to run. */
	THREAD_BLOCKED,     /* Waiting for an event to trigger. */
	THREAD_DYING        /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* File descriptor for file system */
#define FD_BASE 2
#define FD_LIMIT 128

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
struct thread {
	/* Owned by thread.c. */
	tid_t tid;                          /* Thread identifier. */
	enum thread_status status;          /* Thread state. */
	char name[16];                      /* Name (for debugging purposes). */
	int priority;                       /* Priority. */

	/* Lab #1: Threads - Alarm clock */
	int64_t local_tick; 				/* Local tick for thread_sleep */

	/* Lab #1: Threads - Priority- donation */
	int init_priority;					/* Priority backup for nested donation */
	struct lock *waiting_lock;			/* Lock that thread is waiting for */
	struct list donors;					/* List of donors */
	struct list_elem donor_elem;		/* List element for donors */

	/* Lab #1 - advanced 구현에 사용*/
	int nice;
	int recent_cpu;	
	
	/* Shared between thread.c and synch.c. */
	struct list_elem elem;              /* List element. */
	struct list_elem mlfqs_elem;        /* List element for mlfqs threads list. */

#ifdef USERPROG
	/* Owned by userprog/process.c. */
	uint64_t *pml4;                     /* Page map level 4 */
	
	int exit_status;                    /* syscall - Exit status, 0 is success and -1 is fail */

	struct list *fd_table;             /* File descriptor table */

	struct list child_list; 		   /* List of child threads */
	struct list_elem child_elem; 	   /* List element for child threads */

	struct thread *parent; 			  /* Parent thread */
	struct intr_frame parent_if;       /* Parent's intr_frame */

	// NOTE: 반드시 struct semaphore *가 아닌 struct semaphore로 선언해야 함
	// 이유는 struct semaphore *로 선언하면 sema_init() 이후 list_push_back() 또는 list_insert_ordered()에서
	// sema_init()으로 초기화된 semaphore의 주소값이 아닌 이상한 주소값이 들어가게 되어 문제가 발생함
	struct semaphore sema_wait; 	   /* Semaphore for waiting */
	struct semaphore sema_load; 	   /* Semaphore for loading */
	struct semaphore sema_exit; 	   /* Semaphore for exiting */

	struct file *exec_file;            /* Executable file */

#endif
#ifdef VM
	struct supplemental_page_table spt; 	   /* Table for whole virtual memory owned by thread. */
	void *stack_pointer;  /* Stack pointer */
#endif

	/* Owned by thread.c. */
	struct intr_frame tf;               /* Information for switching */
	unsigned magic;                     /* Detects stack overflow. */
};

struct fd_elem {
	int fd;
	struct file *file;
	struct list_elem elem;
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

/* Lab #1 - 함수 정의*/
void thread_sleep (int64_t ticks);
void thread_wake (int64_t local_tick);
void thread_try_preempt(void);

int64_t get_global_tick(void);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

int thread_get_priority (void);
void thread_set_priority (int);
void thread_donate_priority (void);
void thread_update_priority (void);

/* Lab 1 - 함수 정의*/
void advanced_priority_calculation (struct thread *t);
void advanced_recent_cpu_calculation (struct thread *t);
void advanced_load_avg_calculation (void);
void advanced_recent_cpu_increase (void);
void advanced_recent_cpu_update (void);
void advanced_priority_update (void);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void do_iret (struct intr_frame *tf);

#endif /* threads/thread.h */
