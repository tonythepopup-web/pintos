#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/synch.h"

/* States in a thread's life cycle. */
enum thread_status
  {
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

/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+

   The upshot of this is twofold:

      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  It probably should stay well under 1
         kB.

      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */
struct thread
  {
    /* Owned by thread.c. */
    tid_t tid;                          /* Thread identifier. */
    enum thread_status status;          /* Thread state. */
    char name[16];                      /* Name (for debugging purposes). */
    uint8_t *stack;                     /* Saved stack pointer. */
    int priority;                       /* Priority. */
    struct list_elem allelem;           /* List element for all threads list. */

    /* Shared between thread.c and synch.c. */
    struct list_elem elem;              /* List element. */

#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint32_t *pagedir;                  /* Page directory. */
#endif

    /* Owned by thread.c. */
    unsigned magic;                     /* Detects stack overflow. */

    int64_t alarm_ticks;  //알람 울릴 시간

    int init_priority;  //우선순위 기본값
    struct lock* waiting;  //지금 이 스레드가 얻고 싶어서 기다리고 있는 락
    struct list donation_list;  //내가 쓰고 있는 락을 기다리는 스레드 리스트
    struct list_elem donation_elem;  //내가 다른 스레드의 donation_list에 들어갈 때 쓰는 연결 노드

    int nice;  //nice 수치
    int recent_cpu;  //cpu 점유율 수치
    //project2
    struct thread *parent;  //내 부모 스레드를 가리키는 포인터
    struct list child_list;  //내 자식 스레드 리스트
    struct list_elem child_elem;  //내가 다른 스레드의 자식 리스트에 들어갈 때 쓰는 연결 노드

    struct semaphore child_sema;  //부모가 자식을 기다릴 때 쓰는 세마포어
    struct semaphore load_sema;  //부모가 자식이 load되는 동안 쓰는 세마포어
    struct semaphore exit_sema;  //부모가 자식을 제거할 때 쓰는 세마포어

    int exit_status;  //자식 프로세스의 종료 상태
    struct file *running;  //현재 실행 중인 파일
    struct list file_list;  //이 스레드가 연 파일들의 리스트
    int fd_count;  //현재 스레드가 다음에 할당할 파일 디스크립터 번호
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

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

void thread_sleep(int64_t ticks);  //스레드를 sleep시키고 알람 시간 맞춰줌
void thread_alarm(int64_t ticks);  //알람 울렸는지 확인하고 울렸으면 깨워줌

bool compare_priority(const struct list_elem* a, const struct list_elem* b, void* aux);  //ready_list 재정렬용으로 만든 스레드 간 우선순위 비교 함수
void thread_yield_priority(void);  //우선순위가 더 높은 스레드가 ready 상태라면 현재 스레드를 양보시킴  //기능 자체는 synch.c에서 쓰는데 ready_list 변수가 thread.c에 있어서 여기에 선언
void thread_priority_donation(void); //내가 기다리는 락을 거슬러 올라가면서 지금 그 락을 잡고 있는 스레드에게 자신의 우선순위를 기부
void thread_priority_update(void);  //running 스레드의 우선순위를 기부받기 전의 초기 우선순위로 바꾸고 donation_list에서 가장 높은 우선순위를 가지는 스레드의 우선순위를 running 스레드에 기부

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

//고정소수점 연산 매크로들과 함수들
#define INT_MAX ((1 << 31) - 1)
#define INT_MIN (-(1 << 31))
#define F (1 << 14)
int int_to_fp(int n);
int fp_to_int_tozero(int x);
int fp_to_int_nearest(int x);
int fp_add_fp(int x, int y);
int fp_sub_fp(int x, int y);
int fp_add_int(int x, int n);
int fp_sub_int(int x, int n);
int fp_mul_fp(int x, int y);
int fp_mul_int(int x, int n);
int fp_div_fp(int x, int y);
int fp_div_int(int x, int n);

//우선순위, recent_cpu, load_avg를 계산하고 업데이트하기 위한 함수들
void mlfqs_cal_priority(struct thread* t);
void mlfqs_cal_recent_cpu(struct thread* t);
void mlfqs_cal_load_avg(void);
void mlfqs_increase_recent_cpu(void);
void mlfqs_update_recent_cpu(void);
void mlfqs_update_priority(void);

#endif /* threads/thread.h */
