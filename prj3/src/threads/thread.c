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
#include "threads/fixed-point.h"
#ifdef USERPROG
#include "userprog/process.h"
#include "userprog/pagedir.h"
#endif

#define THREAD_MAGIC 0xcd6abf4b

static struct list ready_list;
static struct list all_list;
static struct thread *idle_thread;
static struct thread *initial_thread;
static struct lock tid_lock;

struct kernel_thread_frame 
  {
    void *eip;
    thread_func *function;
    void *aux;
  };

static long long idle_ticks;
static long long kernel_ticks;
static long long user_ticks;

#define TIME_SLICE 4
static unsigned thread_ticks;

bool thread_mlfqs;
bool thread_prior_aging;
static int load_avg;

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
static bool thread_priority_less (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);
static void thread_check_preemption (void);
static void mlfqs_calculate_priority (struct thread *t);
static void mlfqs_calculate_recent_cpu (struct thread *t);
static void mlfqs_calculate_load_avg (void);
static void mlfqs_recalc_priority_all (void);
static void mlfqs_recalc_recent_cpu_all (void);

void
thread_init (void)
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);

  load_avg = 0;

  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}

void
thread_start (void) 
{
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  intr_enable ();

  sema_down (&idle_started);
}

void
thread_tick (void) 
{
  struct thread *t = thread_current ();

  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}

void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

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

  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  if (thread_mlfqs)
    mlfqs_calculate_priority (t);

  thread_unblock (t);

  if (!thread_mlfqs)
    thread_check_preemption ();

  return tid;
}

void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

void
thread_unblock (struct thread *t)
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  list_insert_ordered (&ready_list, &t->elem, thread_priority_less, NULL);
  t->status = THREAD_READY;
  intr_set_level (old_level);
}

const char *
thread_name (void) 
{
  return thread_current ()->name;
}

struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

void
thread_exit (void)
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
  struct thread *cur = thread_current ();
  struct list_elem *e;


  for (e = list_begin (&cur->children); e != list_end (&cur->children);
       e = list_next (e))
    {
      struct thread *child = list_entry (e, struct thread, child_elem);
      if (child->parent == cur) 
        {
          child->parent = NULL;
        }
    }
#endif

  intr_disable ();

#ifdef USERPROG
  sema_up (&thread_current ()->exit_sema);
#endif

  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

void
thread_yield (void)
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;

  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread)
    list_insert_ordered (&ready_list, &cur->elem, thread_priority_less, NULL);
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

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

void
thread_set_priority (int new_priority)
{
  if (thread_mlfqs)
    return;

  enum intr_level old_level = intr_disable ();
  thread_current ()->priority = new_priority;
  thread_check_preemption ();
  intr_set_level (old_level);
}

int
thread_get_priority (void) 
{
  return thread_current ()->priority;
}

void
thread_set_nice (int nice)
{
  enum intr_level old_level = intr_disable ();
  thread_current ()->nice = nice;
  if (thread_mlfqs)
    {
      mlfqs_calculate_priority (thread_current ());
      thread_check_preemption ();
    }
  intr_set_level (old_level);
}

int
thread_get_nice (void)
{
  enum intr_level old_level = intr_disable ();
  int nice = thread_current ()->nice;
  intr_set_level (old_level);
  return nice;
}

int
thread_get_load_avg (void)
{
  enum intr_level old_level = intr_disable ();
  int result = FP_TO_INT_ROUND(FP_MULT_INT(load_avg, 100));
  intr_set_level (old_level);
  return result;
}

int
thread_get_recent_cpu (void)
{
  enum intr_level old_level = intr_disable ();
  int result = FP_TO_INT_ROUND(FP_MULT_INT(thread_current ()->recent_cpu, 100));
  intr_set_level (old_level);
  return result;
}

void
thread_mlfqs_increment_recent_cpu (void)
{
  struct thread *cur = thread_current ();
  if (cur != idle_thread)
    cur->recent_cpu = FP_ADD(cur->recent_cpu, INT_TO_FP(1));
}

void
thread_mlfqs_recalc_load_avg_and_recent_cpu (void)
{
  mlfqs_calculate_load_avg ();
  mlfqs_recalc_recent_cpu_all ();
}

void
thread_mlfqs_recalc_priority (void)
{
  mlfqs_recalc_priority_all ();
}

static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      intr_disable ();
      thread_block ();
      asm volatile ("sti; hlt" : : : "memory");
    }
}

static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();
  function (aux);
  thread_exit ();
}

struct thread *
running_thread (void) 
{
  uint32_t *esp;
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

static void
init_thread (struct thread *t, const char *name, int priority)
{
  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  t->magic = THREAD_MAGIC;

  if (t == initial_thread)
    {
      t->nice = 0;
      t->recent_cpu = 0;
    }
  else if (thread_mlfqs)
    {
      struct thread *cur = running_thread ();
      t->nice = cur->nice;
      t->recent_cpu = cur->recent_cpu;
    }
  else
    {
      t->nice = 0;
      t->recent_cpu = 0;
    }

#ifdef USERPROG
  t->executable = NULL;
  t->saved_pd = NULL;
  t->parent = running_thread ();
  list_init (&t->children);
  t->exit_status = -1;
  t->load_success = false;
  sema_init (&t->load_sema, 0);
  sema_init (&t->exit_sema, 0);
  for(int i=0;i<FDTABLE_SIZE;i++){
    t->fd_table[i]=NULL;
  }
  t->waited_on = false;

  if (t != initial_thread && t->parent != NULL)
    list_push_back (&t->parent->children, &t->child_elem);
#endif

  list_push_back (&all_list, &t->allelem);
}

static void *
alloc_frame (struct thread *t, size_t size) 
{
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

static struct thread *
next_thread_to_run (void) 
{
  if (list_empty (&ready_list))
    return idle_thread;
  else
    return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  cur->status = THREAD_RUNNING;
  thread_ticks = 0;

#ifdef USERPROG
  process_activate ();
#endif

  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread)
    {
      ASSERT (prev != cur);
#ifdef USERPROG
      if (prev->saved_pd != NULL)
      {
        pagedir_destroy(prev->saved_pd);
        prev->saved_pd = NULL;
      }
#endif
    }
}

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

static bool
thread_priority_less (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
  struct thread *ta = list_entry (a, struct thread, elem);
  struct thread *tb = list_entry (b, struct thread, elem);
  return ta->priority > tb->priority;
}

static void
thread_check_preemption (void)
{
  if (intr_context ())
    return;

  if (list_empty (&ready_list))
    return;

  struct thread *cur = thread_current ();
  struct thread *front = list_entry (list_front (&ready_list), struct thread, elem);

  if (cur->priority < front->priority)
    thread_yield ();
}

static void
mlfqs_calculate_priority (struct thread *t)
{
  if (t == idle_thread)
    return;

  int recent_cpu_int = FP_TO_INT_TRUNC(t->recent_cpu);
  t->priority = PRI_MAX - (recent_cpu_int / 4) - (t->nice * 2);
  if (t->priority < PRI_MIN)
    t->priority = PRI_MIN;
  if (t->priority > PRI_MAX)
    t->priority = PRI_MAX;
}

static void
mlfqs_calculate_recent_cpu (struct thread *t)
{
  if (t == idle_thread)
    return;

  int coeff = FP_DIV(FP_MULT_INT(load_avg, 2), FP_ADD(FP_MULT_INT(load_avg, 2), INT_TO_FP(1)));
  t->recent_cpu = FP_ADD(FP_MULT(coeff, t->recent_cpu), INT_TO_FP(t->nice));
}

static void
mlfqs_calculate_load_avg (void)
{
  int ready_threads = list_size (&ready_list);
  if (thread_current () != idle_thread)
    ready_threads++;

  load_avg = FP_ADD(FP_MULT(FP_DIV(INT_TO_FP(59), INT_TO_FP(60)), load_avg),
                    FP_MULT_INT(FP_DIV(INT_TO_FP(1), INT_TO_FP(60)), ready_threads));
}

static void
mlfqs_recalc_priority_all (void)
{
  struct list_elem *e;
  for (e = list_begin (&all_list); e != list_end (&all_list); e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      mlfqs_calculate_priority (t);
    }
}

static void
mlfqs_recalc_recent_cpu_all (void)
{
  struct list_elem *e;
  for (e = list_begin (&all_list); e != list_end (&all_list); e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      mlfqs_calculate_recent_cpu (t);
    }
}

struct tid_search_data {
  tid_t tid;
  struct thread* result;
};

static void
find_thread_by_tid_action(struct thread* t, void* aux) {
    struct tid_search_data* data = (struct tid_search_data*)aux;
    if (t->tid == data->tid) {
        data->result = t;
    }
}

struct thread*
thread_get_by_tid(tid_t tid) {
    struct tid_search_data data = { .tid = tid, .result = NULL };

    enum intr_level old_level = intr_disable();
    thread_foreach(find_thread_by_tid_action, &data);
    intr_set_level(old_level);

    return data.result;
}

void
thread_aging (void)
{
  if (list_empty (&ready_list))
    return;

  struct list temp_list;
  list_init (&temp_list);

  while (!list_empty (&ready_list))
    {
      struct thread *t = list_entry (list_pop_front (&ready_list), struct thread, elem);
      if (t->priority < PRI_MAX)
        t->priority++;
      list_push_back (&temp_list, &t->elem);
    }

  while (!list_empty (&temp_list))
    {
      struct thread *t = list_entry (list_pop_front (&temp_list), struct thread, elem);
      list_insert_ordered (&ready_list, &t->elem, thread_priority_less, NULL);
    }
}

#ifdef USERPROG
struct thread *
get_child_thread (tid_t child_tid)
{
  struct thread *cur = thread_current ();
  struct list_elem *e;

  for (e = list_begin (&cur->children); e != list_end (&cur->children);
       e = list_next (e))
  {
    struct thread *child = list_entry (e, struct thread, child_elem);
    if (child->tid == child_tid)
      return child;
  }
  return NULL;
}
#endif

uint32_t thread_stack_ofs = offsetof (struct thread, stack);