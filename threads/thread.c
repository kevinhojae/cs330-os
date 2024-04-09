#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* Lab #1 - sleep_list : running 되던 thread가 timer_sleep()을 만나면 이 list에 들어가게 됨.*/
static struct list sleep_list;

/* Lab #1 - mlfqs_list : mlfqs에서 사용하기 위한 list.*/
static struct list mlfqs_list;


/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Lab #1 - global_tick 정의*/
static int64_t global_tick;

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

int load_avg;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);

/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))


// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) {
	ASSERT (intr_get_level () == INTR_OFF);

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);

	/* Init the globla thread context */
	lock_init (&tid_lock);
	list_init (&ready_list);
	list_init (&sleep_list);
	list_init (&mlfqs_list);
	list_init (&destruction_req);

	/* Lab #1 - 이곳에서 다른 global들도 init됨. -> global_tick도 여기서 init*/

	global_tick = INT64_MAX;

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread ();
	init_thread (initial_thread, "main", PRI_DEFAULT);

	list_push_back (&mlfqs_list, &initial_thread->mlfqs_elem);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) {
	/* Create the idle thread. */
	struct semaphore idle_started;
	sema_init (&idle_started, 0);
	thread_create ("idle", PRI_MIN, idle, &idle_started);

	/*Lab 1 - load_avg set defalut*/
	load_avg = 0;

	/* Start preemptive thread scheduling. */
	intr_enable ();

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) {
	struct thread *t = thread_current ();

	/* Update statistics. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* Enforce preemption. */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
		thread_func *function, void *aux) {
	struct thread *t;
	tid_t tid;

	ASSERT (function != NULL);

	/* Allocate thread. */
	t = palloc_get_page (PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* Initialize thread. */
	init_thread (t, name, priority);

	if (thread_mlfqs) {
		struct thread *curr_thread = thread_current ();
		//현재 쓰레드의 nice, recent_cpu를 새로운 쓰레드에 복사
		t->nice = curr_thread->nice;
		t->recent_cpu = curr_thread->recent_cpu;

		//새로운 쓰레드의 priority를 계산
		advanced_priority_calculation(t);

		//mlfqs_list에 추가 (idle 쓰레드 제외)
		if (function != idle)
			list_push_back(&mlfqs_list, &t->mlfqs_elem);
	}

	tid = t->tid = allocate_tid ();

	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	t->tf.rip = (uintptr_t) kernel_thread;
	t->tf.R.rdi = (uint64_t) function;
	t->tf.R.rsi = (uint64_t) aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	// Initialize fd table
	#ifdef USERPROG
	t->fd_table = (struct list*) malloc(sizeof(struct list));
	if (t->fd_table == NULL) {
		return TID_ERROR;
	}

	t->next_fd = 2; // 0, 1 is reserved for stdin, stdout
	list_init(t->fd_table);
	#endif


	// fork시 thread_current()는 parent thread
	// t는 새로 생성되는 child thread
	t->parent = thread_current ();
	list_push_back (&thread_current ()->child_list, &t->child_elem); // add to the child list of the parent thread

	/* Add to run queue. */
	thread_unblock (t); // NOTE: unblock the new thread to add it to the ready list.
	thread_try_preempt ();

	return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) {
	ASSERT (!intr_context ());
	
	/* Lab #1 - 직접 구현한 곳 아님. 여기서도 intr이 off되어있는지 확인함. block 작업이 intr off된 상태에서 이루어져야 함.
	들어가기 전에 off 시켜야 함.*/

	ASSERT (intr_get_level () == INTR_OFF);
	thread_current ()->status = THREAD_BLOCKED;

	schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) {
	enum intr_level old_level;

	ASSERT (is_thread (t));

	old_level = intr_disable ();
	ASSERT (t->status == THREAD_BLOCKED);
	// list_push_back (&ready_list, &t->elem);
	list_insert_ordered(&ready_list, &t->elem, (list_less_func *) &compare_thread_priority_desc, NULL);
	t->status = THREAD_READY;
	intr_set_level (old_level);
}
/* Lab #1 - 이거 수정하는 줄 알았는데 아니었다.
새로 thread를 sleep_list로 넣어주는 함수를 만들고 그 함수에서 block과 unblock을 사용하는 듯 하다.*/

/* Lab #1 - 쓰레드를 sleep_list로 보내는 작용
해야할 task 목록
1. timer_sleep에게 호출당함
2. idle인지 확인부터 해야 함
3. 인터럽트 막도록 해야함
4. 외부 인터럽트 체크
5. local tick
6. sleep_list에 넣어주기
7. block
8. 인터럽트 해제*/
void
thread_sleep (int64_t ticks) {
	/* 현재 실행하고 있는 쓰레드에 대한 작업. 이를 thread *cur에 thread_current()로 받아왔다.*/

	struct thread *curr_thread = thread_current ();

	/* 일단 idle인지 check 부터 해야 함.
	if를 쓰려니 그냥 넘어가게 할 방법을 찾을 수 없었음.
	더 쉬운 방법 발견. assert는 condition 상태이면 넘어가고 아니면 error 호출함.
	추가 질문: 그러나 assert가 완전한 방법인지 의문이 있음. assert 함수를 살펴보니 os를 중지시킬 뿐이며 직접적인 해결책은 아닌 것 같은데?
	일단 타 함수들에서 사용하는 방법이기에 채용. */

	ASSERT(curr_thread != idle_thread);

	/* idle 아니라고 확정되면 인터럽트 막아야 함.
	타 함수들 참고하니 old_level 방식을 사용해서 인터럽트로 들어오는 방해를 막고 있음 */
	
	enum intr_level old_level;

	/* condition 상태이면 그냥 넘어가고 아니면 error를 부르는 assert 이용.
	외부 인터럽트를 처리하는 중이 아니면 계속 이어가도록 되어 있음.*/

	ASSERT (!intr_context ());

	/*point. old_level에 인터럽트 disable을 넣어서 이거 사용하게 한다.
	정확히는 현재 상태를 disable로 설정시키면서 이전 상태(able)를 반환한다. 이전 상태가 old_level에 저장됨.*/
	old_level = intr_disable ();

	/* local_tick에 현재 ticks를 넣어주면서*/
	curr_thread -> local_tick = ticks;
	/* 이 local_tick이 global_tick보다 작은지 체크하고, global_tick을 가장 작은 값으로 재정의 시켜야 한다.*/
	if (curr_thread->local_tick < global_tick) {
		global_tick = curr_thread->local_tick;
	}

	// sleep_list를 local_tick을 기준으로 오름차순하여 정렬
	//list_push_back(&sleep_list, &curr_thread->elem);
	list_insert_ordered(&sleep_list, &curr_thread->elem, (list_less_func *) &compare_local_tick_asc, NULL);

	/*이제 block을 시켜야 한다.*/
	thread_block();

	/*마지막으로 인터럽트를 다시 풀어줘야 한다.*/
	intr_set_level(old_level);
}

/* Lab #1 - 쓰레드를 sleep_list에서 ready_list로 보내는 작용*/
void
thread_wake(int64_t tick) {
	/*global tick 변수 사용하고.*/
	global_tick = INT64_MAX;

	/*list 변수 사용*/
	struct list_elem *element_from_sleep_list;
	/*슬립 리스트의 요소 처음 것 take.*/
	element_from_sleep_list = list_begin(&sleep_list);

	/*리스트의 끝까지 원소 하나하나 체크.
	다만, list 정렬된 상태이기에 wake up할 시간이 안 된 쓰레드를 처음 만난 이후부터는 확인할 필요 없음-> break*/
	while (element_from_sleep_list != list_end(&sleep_list)) {
		struct thread *cur = list_entry(element_from_sleep_list, struct thread, elem);

		//해당 파트에서는 cur의 local_tick이 제시된 tick보다 작거나 같으면
		if (cur->local_tick <= tick) {
			//해당 리스트에서 제거.
			element_from_sleep_list = list_remove(&cur->elem);
			thread_unblock(cur);
		}
		else {
			/* wakeup하지 않아도 되는 쓰레드 만날 경우 : global tick update 해주고, break */
			//element_from_sleep_list = list_next(element_from_sleep_list);
			if (cur->local_tick < global_tick) {
				global_tick = cur->local_tick;
			}

			break;
		}
	}
}


/* preempt the current thread if the ready list is not empty and the highest priority thread has higher priority than the current thread */
void 
thread_try_preempt (void) {
	if (list_empty (&ready_list)) {
		return;
	}

	// NOTE: Must use list_begin, not list_front, because list_front is not safe when the list is empty.
	struct thread* highest_ready_thread = list_entry (list_begin (&ready_list), struct thread, elem);
	if (!intr_context () && thread_current ()->priority < highest_ready_thread->priority) {
		thread_yield ();
	}
}


/* Lab #1 - global tick 가져오기..*/
int64_t
get_global_tick (void) {
	return global_tick;
}

/* Returns the name of the running thread. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) {
	struct thread *t = running_thread ();

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	ASSERT (is_thread (t));
	ASSERT (t->status == THREAD_RUNNING);

	return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) {
	return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) {
	ASSERT (!intr_context ());

#ifdef USERPROG
	process_exit ();
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable ();
	
	if (thread_mlfqs) {
		// remove the thread from the mlfqs_list
		list_remove(&thread_current()->mlfqs_elem);
	}

	do_schedule (THREAD_DYING);
	NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) {
	struct thread *curr = thread_current ();
	enum intr_level old_level;

	ASSERT (!intr_context ());

	old_level = intr_disable ();
	if (curr != idle_thread) {
		// list_push_back (&ready_list, &curr->elem);
		list_insert_ordered(&ready_list, &curr->elem, (list_less_func *) &compare_thread_priority_desc, NULL);
	}
	do_schedule (THREAD_READY);
	intr_set_level (old_level);
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) {
	//mlfqs는 priority 사용법이 다르기에 바로 나가준다.
	if (thread_mlfqs) {
		return;
	}
	
	// thread가 가지고 있는 lock을 기다리고 있는 다른 thread가 있다면 (=thread의 donors 리스트가 존재한다면)
	// thread의 priorty는 새로운 priority로 바뀌는 것이 아닌 init_priority로 보관해두고, donor 리스트가 비워지면 priority를 init_priority로 바꾼다.
	thread_current ()->init_priority = new_priority;
	
	// reorder the ready list for the new priority
	if (!list_empty(&ready_list)) {
		list_sort(&ready_list, (list_less_func *) &compare_thread_priority_desc, NULL);
		thread_update_priority (); // 여기서 thread의 priority를 실제로 업데이트한다.
		thread_try_preempt ();
	}
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) {
	return thread_current ()->priority;
}

/* Lab 1 - advanced scheduler - priority calculation*/
void
advanced_priority_calculation (struct thread *t) {
	//항상 필요한 idle check.
	if (t == idle_thread) {
		return;
	}
	//계산. nice는 int, but recent_cpu가 float이기에 값 맞춰주기.
	t->priority = ((63 - t->nice*2)*(1<<14) + t->recent_cpu /(-4)) / (1<<14);
}

/* Lab 1 - advanced scheduler - recent_cpu calculation*/
void
advanced_recent_cpu_calculation (struct thread *t) {
	//idle check
	if (t == idle_thread) {
		return;
	}

	//복잡한 계산식.
	int L_a = load_avg*2;
	t->recent_cpu = ((int64_t)( ((int64_t)(L_a)) * (1<<14) / (L_a+(1<<14)) ) ) * (t->recent_cpu)/(1<<14) + (t->nice)*(1<<14);
}

/* Lab 1 - advanced scheduler - load_avg calculation*/
void
advanced_load_avg_calculation (void) {
	int ready_threads;
	
	if (thread_current() != idle_thread) {
		ready_threads = list_size(&ready_list) + 1;
	}
	else {
		ready_threads = list_size(&ready_list);
	}

	load_avg = ( ((int64_t)( ( (int64_t)(59*(1<<14)) ) * (1<<14) / (60*(1<<14)) )) * load_avg / (1<<14) )
					+ ( ((int64_t)(1<<14)) * (1<<14) / (60*(1<<14)) ) * ready_threads;
}

/* Lab 1 - advanced scheduler - recent_cpu increase*/
void
advanced_recent_cpu_increase (void) {
	//idle check -> if it isn't
	if (thread_current() != idle_thread) {
		//current thread의 recent_cpu 1 증가 (float표현)
		thread_current()->recent_cpu += (1<<14);
	}
}

/* Lab 1 - advanced scheduler - update recent_cpu*/
void
advanced_recent_cpu_update (void) {
	struct list_elem *e;
	struct thread *t;

	//list 시작부에서 끝까지 체크.
	for (e = list_begin(&mlfqs_list); e != list_end(&mlfqs_list); e = list_next(e)) {
		//list_entry 매크로를 사용해서 mlfqs_elem에서 해당하는 thread를 가져온다.
		t = list_entry(e, struct thread, mlfqs_elem);
		//해당 값에 대해 recent_cpu를 계산한다.
		advanced_recent_cpu_calculation(t);
	}
}

/* Lab 1 - advanced scheduler - priority update*/
void
advanced_priority_update (void) {
	struct list_elem *e;
	struct thread *t;

	//mlfq 리스트의 인스턴스들을 처음부터 끝까지 실행시킨다.
	for (e = list_begin(&mlfqs_list); e != list_end(&mlfqs_list); e = list_next(e)) {
		// list_entry 매크로를 사용해서 mlfqs_elem에서 해당하는 thread를 가져온다.
		t = list_entry(e, struct thread, mlfqs_elem);
		//해당 값에 대해 priority를 계산한다.
		advanced_priority_calculation(t);
	}
}

/** Donate the priority to the holder of the lock.
 * This function is called when the current thread attempts to acquire a lock.
 * The current thread donates its priority to the holder of the lock.
 */
void
thread_donate_priority (void) {
	struct thread *current_thread = thread_current ();
	int new_priority = current_thread->priority; // donate할 priority
	
	struct thread *holder;
	int NESTED_DEPTH = 8; //NOTE: nested depth를 지정해주지 않고 그냥 for문으로 waiting_lock이 null이 될 때까지 돌리면 kernel panic
    for (int i = 0; i < NESTED_DEPTH; i++) {
        if (current_thread->waiting_lock == NULL) {
            return;
		}

		holder = current_thread->waiting_lock->holder; // lock을 가지고 있는 thread
		holder->priority = new_priority; // holder의 priority를 donate할 priority로 업데이트
		current_thread = holder; // current_thread를 holder로 변경
    }
}

/** Update the priority of the current thread based on the donors list.
 * This function is called in the following cases:
 * 1. When the current thread releases the lock it holds.
 * 2. When the current thread's priority is set to a new value.
 */
void
thread_update_priority (void) {
	struct thread* current_thread = thread_current ();

	// thread가 donors 리스트를 가지고 있지 않다면(=자신이 가지고 있는 lock을 기다리는 다른 thread가 없다면) init_priority로 돌아감
	if (list_empty(&(current_thread->donors))) {
		current_thread->priority = current_thread->init_priority;
		return;
	}

	// donors는 lock_acquire() 에서 정렬되었으므로, 가장 높은 priority를 가진 thread는 가장 앞의 thread
	struct thread* highest_priority_thread = list_entry(list_front(&current_thread->donors), struct thread, donor_elem);

	// multiple donation case, 물려있는 여러 개의 lock 중 하나가 release되면, 그 lock의 donor을 제외한 나머지 donor들 중 가장 높은 priority를 가져와서,
	// 본인의 최초 priority와 가장 높은 donor priority를 비교하여 더 높은 priority로 업데이트
	if (highest_priority_thread->priority > current_thread->init_priority) {
		current_thread->priority = highest_priority_thread->priority;
	}
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) {
	//인터럽트 막기
	enum intr_level old_level = intr_disable();

	//현재 쓰레드 nice 설정
	thread_current()->nice = nice;

	//우선순위 설정
	advanced_priority_calculation(thread_current());

	//우선순위에 따라 선점 시도
	thread_try_preempt();

	//인터럽트 해제
	intr_set_level(old_level);
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) {
	//인터럽트 막기
	enum intr_level old_level = intr_disable();

	//nice값 가져오기.
	int take_nice = thread_current()->nice;

	//인터럽트 해제
	intr_set_level(old_level);

	//int형 리턴.
	return take_nice;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) {
	//인터럽트 막기
	enum intr_level old_level = intr_disable();

	//load_avg 가져와서 100 곱하기. (실질적 float형)
	int load_avg_mul100 = load_avg * 100;

	//float값 int로 변환.
	if (load_avg_mul100 >= 0) {
		load_avg_mul100 = (load_avg_mul100 + (1<<14)/2) / (1<<14);
	}
	else {
		load_avg_mul100 = (load_avg_mul100 - (1<<14)/2) / (1<<14);
	}

	//인터럽트 해제
	intr_set_level(old_level);

	//int형 리턴
	return load_avg_mul100;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) {
	//인터럽트 막기
	enum intr_level old_level = intr_disable();

	//현재 쓰레드 recent_cpu 가져와서 100 곱하기. (실질적 float형)
	int recent_cpu_mul100 = (thread_current()->recent_cpu)*100;

	//recent_cpu 값은 float --> int로 변환
	if (recent_cpu_mul100 >= 0) {
		recent_cpu_mul100 = (recent_cpu_mul100 + (1<<14)/2) / (1<<14);
	}
	else {
		recent_cpu_mul100 = (recent_cpu_mul100 - (1<<14)/2) / (1<<14);
	}

	//인터럽트 해제
	intr_set_level(old_level);

	//int형 리턴
	return recent_cpu_mul100;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current ();
	sema_up (idle_started);

	for (;;) {
		/* Let someone else run. */
		intr_disable ();
		thread_block ();

		/* Re-enable interrupts and wait for the next one.

		   The `sti' instruction disables interrupts until the
		   completion of the next instruction, so these two
		   instructions are executed atomically.  This atomicity is
		   important; otherwise, an interrupt could be handled
		   between re-enabling interrupts and waiting for the next
		   one to occur, wasting as much as one clock tick worth of
		   time.

		   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		   7.11.1 "HLT Instruction". */
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* The scheduler runs with interrupts off. */
	function (aux);       /* Execute the thread function. */
	thread_exit ();       /* If function() returns, kill the thread. */
}


/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
	t->priority = priority;
	t->init_priority = priority;
	t->waiting_lock = NULL;
	list_init(&(t->donors));
	t->magic = THREAD_MAGIC;
	
	//초기값 설정하기
	t->nice = 0;
	t->recent_cpu = 0;

#ifdef USERPROG
	// t->exit_status = 0;
	list_init(&t->child_list); // initialize the child list
	sema_init(&t->sema_wait, 0); // initialize the semaphore for waiting
	sema_init(&t->sema_load, 0); // initialize the semaphore for child process fork and loads
	sema_init(&t->sema_exit, 0); // initialize the semaphore for exit
#endif
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	else
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
void
do_iret (struct intr_frame *tf) {
	__asm __volatile(
			"movq %0, %%rsp\n"
			"movq 0(%%rsp),%%r15\n"
			"movq 8(%%rsp),%%r14\n"
			"movq 16(%%rsp),%%r13\n"
			"movq 24(%%rsp),%%r12\n"
			"movq 32(%%rsp),%%r11\n"
			"movq 40(%%rsp),%%r10\n"
			"movq 48(%%rsp),%%r9\n"
			"movq 56(%%rsp),%%r8\n"
			"movq 64(%%rsp),%%rsi\n"
			"movq 72(%%rsp),%%rdi\n"
			"movq 80(%%rsp),%%rbp\n"
			"movq 88(%%rsp),%%rdx\n"
			"movq 96(%%rsp),%%rcx\n"
			"movq 104(%%rsp),%%rbx\n"
			"movq 112(%%rsp),%%rax\n"
			"addq $120,%%rsp\n"
			"movw 8(%%rsp),%%ds\n"
			"movw (%%rsp),%%es\n"
			"addq $32, %%rsp\n"
			"iretq"
			: : "g" ((uint64_t) tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
static void
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile (
			/* Store registers that will be used. */
			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"
			/* Fetch input once */
			"movq %0, %%rax\n"
			"movq %1, %%rcx\n"
			"movq %%r15, 0(%%rax)\n"
			"movq %%r14, 8(%%rax)\n"
			"movq %%r13, 16(%%rax)\n"
			"movq %%r12, 24(%%rax)\n"
			"movq %%r11, 32(%%rax)\n"
			"movq %%r10, 40(%%rax)\n"
			"movq %%r9, 48(%%rax)\n"
			"movq %%r8, 56(%%rax)\n"
			"movq %%rsi, 64(%%rax)\n"
			"movq %%rdi, 72(%%rax)\n"
			"movq %%rbp, 80(%%rax)\n"
			"movq %%rdx, 88(%%rax)\n"
			"pop %%rbx\n"              // Saved rcx
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"              // Saved rbx
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"              // Saved rax
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"
			"movw %%es, (%%rax)\n"
			"movw %%ds, 8(%%rax)\n"
			"addq $32, %%rax\n"
			"call __next\n"         // read the current rip.
			"__next:\n"
			"pop %%rbx\n"
			"addq $(out_iret -  __next), %%rbx\n"
			"movq %%rbx, 0(%%rax)\n" // rip
			"movw %%cs, 8(%%rax)\n"  // cs
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n" // eflags
			"mov %%rsp, 24(%%rax)\n" // rsp
			"movw %%ss, 32(%%rax)\n"
			"mov %%rcx, %%rdi\n"
			"call do_iret\n"
			"out_iret:\n"
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
static void
do_schedule(int status) {
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (thread_current()->status == THREAD_RUNNING);
	while (!list_empty (&destruction_req)) {
		struct thread *victim =
			list_entry (list_pop_front (&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	thread_current ()->status = status;
	schedule ();
}

static void
schedule (void) {
	/* 현재 실행하는 쓰레드를 넣고. 다음에 실행할 쓰레드도 넣음 */

	struct thread *curr = running_thread ();
	struct thread *next = next_thread_to_run ();

	/*일단 intr_off인지 상태를 확인함.
	그리고 현재 상태가 쓰레드 실행중이 아니어야 함을 체크. schedule할 때는 보통 인터럽트로 cpu 점유할 쓰레드 바뀔때이니 아니어야 하는듯.
	이후 다음 쓰레드가 옳은 것이 맞는지 체크? null이 아니어야 함. 또한 Thread_magic 확인. (Thread_magic?)
	-> guide 내용: overflow 검출*/

	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (curr->status != THREAD_RUNNING);
	ASSERT (is_thread (next));
	/* Mark us as running.  다음 쓰레드의 상태를 실행으로 변경.*/
	next->status = THREAD_RUNNING;

	/* Start new time slice. 새로운 thread_ticks을 선언. 이를 통해 이번 쓰레드가 cpu 얼마나 점유했는지 확인 가능.*/
	thread_ticks = 0;

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate (next);
#endif

	if (curr != next) {
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used by the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT (curr != next);
			list_push_back (&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch (next);
	}
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}
