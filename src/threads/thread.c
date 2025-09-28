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
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

static struct list sleeping_list;  //sleep된 스레드 리스트

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;
int load_avg;  //얼마나 많은 스레드가 CPU 실행을 기다리고 있는지 나타냄

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

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
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);
  list_init(&sleeping_list);  //sleeping_list도 초기화

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);

  load_avg = 0;  //load_avg 초기화
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) 
{
  struct thread *t = thread_current ();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
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
thread_print_stats (void) 
{
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
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  /* Add to run queue. */
  thread_unblock (t);

  if (t->priority > thread_current()->priority) {
    thread_yield();
  }

  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
  ASSERT (!intr_context ());
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
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  //list_push_back (&ready_list, &t->elem);  //이제 리스트에 우선순위 순서대로 스레드를 넣어야 함
  list_insert_ordered(&ready_list, &t->elem, compare_priority, 0);  //lib/kernel/list.c에 보면 있는 함수
  t->status = THREAD_READY;
  intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
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
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread)
  {
      //list_push_back (&ready_list, &cur->elem);  //이제 리스트에 우선순위 순서대로 스레드를 넣어야 함
      list_insert_ordered(&ready_list, &cur->elem, compare_priority, 0);  //lib/kernel/list.c에 보면 있는 함수
  }
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

void
thread_sleep(int64_t ticks)
{
    enum intr_level old_level;  //변수 선언
    old_level = intr_disable();  //인터럽트 저장+끄기
    struct thread* cur = thread_current();  //현재 실행 중인 스레드 가져오기
    ASSERT(cur != idle_thread);  //idle thread는 sleep시키면 안됨
    cur->alarm_ticks = ticks;  //알람 울릴 시점 지정
    list_push_back(&sleeping_list, &cur->elem);  //sleeping_list에 스레드 추가
    thread_block();  //스레드 상태를 blocked로 전환
    intr_set_level(old_level);  //인터럽트 복구
}

void
thread_alarm(int64_t ticks)
{
    struct list_elem* ele = list_begin(&sleeping_list);  //sleeping_list에 들어있는 스레드 중 첫번째
    struct thread* t;  //thread 변수 선언

    while (ele != list_end(&sleeping_list))  //sleeping_list 끝까지 순회
    {
        t = list_entry(ele, struct thread, elem);  //지금 순회중인 스레드
        if (ticks < t->alarm_ticks)  //아직 알람 시간이 아님
        {
            ele = list_next(ele);  //다음 원소로 이동
        }
        else  //알람 시간임
        {
            ele = list_remove(ele);  //리스트에서 제거
            thread_unblock(t);  //스레드 상태 unblock으로 전환
        }
    }
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) 
{
    if(thread_mlfqs) return;
    thread_current()->init_priority = new_priority;  //일단 running 스레드의 기본 우선순위를 갱신
    thread_priority_update();  //현재 스레드의 우선순위가 변경되었으므로 donation을 다시 수행

    if (!list_empty(&ready_list))  //ready_list 비어있는지 확인
    {
        if (list_entry(list_front(&ready_list), struct thread, elem)->priority > thread_get_priority())  //ready_list에 running 스레드보다 우선순위 더 높은 스레드 있는지 확인
        {
            thread_yield();  //있으면 양보해줌
        }
    }
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  return thread_current ()->priority;
}

bool compare_priority(const struct list_elem* a, const struct list_elem* b, void* aux UNUSED)
{
    return (list_entry(a, struct thread, elem)->priority > list_entry(b, struct thread, elem)->priority);  //우선순위 비교
}

void thread_yield_priority()
{
    if (!list_empty(&ready_list))  //ready_list 비어있는지 확인
    {
        if (list_entry(list_front(&ready_list), struct thread, elem)->priority > thread_get_priority())  //ready_list에 running 스레드보다 우선순위 더 높은 스레드 있는지 확인
        {
            thread_yield();  //있으면 양보해줌
        }
    }
}

void thread_priority_donation()
{
    int depth = 0;  //락을 몇 번째까지 거슬러 올라갔는지 세는 변수
    struct thread* t = thread_current();  //running thread 선언
    int curr;  //현재 스레드의 우선순위 저장하는 변수
    curr = thread_get_priority();  //현재 우선순위 저장

    while (depth != 10)  //최대 donation 깊이는 10으로 제한
    {
        if (!t->waiting)  //이 스레드가 아무 락도 안 기다리고 있으면
        {
            break;  //donation 중단
        }
        t = t->waiting->holder;  //running 스레드가 대기 중인 락을 가지고 있는 스레드
        t->priority = curr;  //현재 우선순위를 락을 가진 스레드에게 전달
        depth++;  //깊이 갱신
    }
}

void thread_priority_update()
{
    struct thread* t = thread_current();  //running thread 선언
    t->priority = t->init_priority;  //일단 running 스레드의 우선순위를 donation 받기 전으로 초기화
    if (!list_empty(&t->donation_list))  //donation_list가 안 비어있으면
    {
        list_sort(&t->donation_list, &compare_priority, NULL);  //lib/kernel/list.c에 있는 정렬 함수
        struct thread* thr = list_entry(list_front(&t->donation_list), struct thread, donation_elem);  //donation_list에서 가장 높은 우선순위를 가진 스레드 선언
        if (thr->priority > thread_get_priority())  //기부하는 스레드의 우선순위가 running 스레드 우선순위보다 크면
        {
            t->priority = thr->priority;  //running 스레드 우선순위 갱신
        }
    }
}

//고정소수점 연산 함수들
int int_to_fp(int n)
{
    return n * F;
}

int fp_to_int_tozero(int x)
{
    return x / F;
}

int fp_to_int_nearest(int x)
{
    return x >= 0 ? (x + F / 2) / F : (x - F / 2) / F;
}

int fp_add_fp(int x, int y)
{
    return x + y;
}

int fp_sub_fp(int x, int y)
{
    return x - y;
}

int fp_add_int(int x, int n)
{
    return x + n * F;
}

int fp_sub_int(int x, int n)
{
    return x - n * F;
}

int fp_mul_fp(int x, int y)
{
    return ((int64_t)x) * y / F;
}

int fp_mul_int(int x, int n)
{
    return x * n;
}

int fp_div_fp(int x, int y)
{
    return ((int64_t)x) * F / y;
}

int fp_div_int(int x, int n)
{
    return x / n;
}

void
mlfqs_cal_priority(struct thread* t)
{
    if (t != idle_thread)  //idle 스레드가 아니라면
    {
        int fp_prior = int_to_fp(PRI_MAX) - fp_div_int(t->recent_cpu, 4) -  int_to_fp(t->nice * 2);  //priority = PRI_MAX - (recent_cpu / 4) - (nice * 2)
        int int_prior = fp_to_int_tozero(fp_prior);
        if(int_prior > PRI_MAX) int_prior = PRI_MAX;
        if(int_prior < PRI_MIN) int_prior = PRI_MIN; 
        t->priority = int_prior;   
    }
}

void
mlfqs_cal_recent_cpu(struct thread* t)
{
    if (t != idle_thread)  //idle 스레드가 아니라면
    {
        t->recent_cpu = fp_add_int(fp_mul_fp(fp_div_fp(fp_mul_int(load_avg, 2), fp_add_int(fp_mul_int(load_avg, 2), 1)), t->recent_cpu), t->nice);
        //recent_cpu = (2*load_avg)/(2*load_avg + 1) * recent_cpu + nice
    }
}

void
mlfqs_cal_load_avg(void)
{
    int ready_threads = list_size(&ready_list);  //ready 상태인 스레드의 개수
    if (thread_current() != idle_thread)  //idle 스레드가 아니라면
    {
        ready_threads++;  //개수 추가
    }
    load_avg = fp_add_fp(fp_mul_fp(fp_div_fp(int_to_fp(59), int_to_fp(60)), load_avg), fp_mul_fp(fp_div_fp(int_to_fp(1), int_to_fp(60)), int_to_fp(ready_threads)));
    //load_avg = (59 / 60) * load_avg + (1 / 60) * ready_threads
}

void
mlfqs_increase_recent_cpu(void)
{
    if (thread_current() != idle_thread)  //idle 스레드가 아니라면
    {
        thread_current()->recent_cpu = fp_add_int(thread_current()->recent_cpu, 1);  //이 스레드가 이번 tick 동안 cpu를 썼기 때문에 점유율 상승
    }
}

void
mlfqs_update_recent_cpu(void)
{
    struct list_elem* ele = list_begin(&all_list);  //존재하는 모든 스레드의 리스트 시작 노드
    while (ele != list_end(&all_list))  //리스트가 끝날 때까지
    {
        struct thread* t = list_entry(ele, struct thread, allelem);  //현재 순회 중인 스레드 선언
        mlfqs_cal_recent_cpu(t);  //recent_cpu 계산
        ele = list_next(ele);  //다음 노드로 넘어감
    }
}

void
mlfqs_update_priority(void)
{
    struct list_elem* ele = list_begin(&all_list);  //존재하는 모든 스레드의 리스트 시작 노드
    while (ele != list_end(&all_list))  //리스트가 끝날 때까지
    {
        struct thread* t = list_entry(ele, struct thread, allelem);  //현재 순회 중인 스레드 선언
        mlfqs_cal_priority(t);  //우선순위 계산
        ele = list_next(ele);  //다음 노드로 넘어감
    }
}


/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice) 
{
    struct thread* t = thread_current();  //현재 실행 중인 스레드 가져오기
    enum intr_level old_level;  //인터럽트 변수 선언
    old_level = intr_disable();  //인터럽트 저장+끄기

    t->nice = nice;  //nice 값 업데이트
    mlfqs_cal_priority(t);  //새로운 nice, recent_cpu 값으로 우선순위 다시 계산

    if (!list_empty(&ready_list))  //readt list에 스레드가 있으면
    {
        struct thread* thr = list_entry(list_front(&ready_list), struct thread, elem);  //ready list에서 우선순위가 가장 높은 스레드 선언
        if (thr->priority > t->priority)  //더 높은 우선순위를 가진 스레드가 있으면
        {
            thread_yield();  //양보
        }
    }
    intr_set_level(old_level);  //인터럽트 복구
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
    struct thread* t = thread_current();  //현재 실행 중인 스레드 가져오기
    enum intr_level old_level;  //인터럽트 변수 선언
    old_level = intr_disable();  //인터럽트 저장+끄기

    int nice = t->nice;  //현재 스레드의 nice 값 받아오기

    intr_set_level(old_level);  //인터럽트 복구
    return nice;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
    enum intr_level old_level;  //인터럽트 변수 선언
    old_level = intr_disable();  //인터럽트 저장+끄기

    int int_load_avg = fp_to_int_nearest(fp_mul_int(load_avg, 100));  //load_avg에 100을 곱하고 근사해서 정수로 변환

    intr_set_level(old_level);  //인터럽트 복구
    return int_load_avg;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
    struct thread* t = thread_current();  //현재 실행 중인 스레드 가져오기
    enum intr_level old_level;  //인터럽트 변수 선언
    old_level = intr_disable();  //인터럽트 저장+끄기

    int int_recent_cpu = fp_to_int_nearest(fp_mul_int(t->recent_cpu, 100));  //현재 스레드의 recent_cpu에 100을 곱하고 근사해서 정수로 변환

    intr_set_level(old_level);  //인터럽트 복구
    return int_recent_cpu;
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
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
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
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  enum intr_level old_level;

  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  //
  t->init_priority = priority;
  list_init(&t->donation_list);
  t->waiting = NULL;
  t->alarm_ticks = 0;

  //
  t->magic = THREAD_MAGIC;
  t->nice = 0;  //nice 값 초기화
  t->recent_cpu = 0;  //cpu 점유율 초기화

  old_level = intr_disable ();
  list_push_back (&all_list, &t->allelem);
  intr_set_level (old_level);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
  if (list_empty (&ready_list))
    return idle_thread;
  else
    return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);
