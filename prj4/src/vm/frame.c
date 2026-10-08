#include "vm/frame.h"
#include "threads/synch.h"
#include "threads/palloc.h"
#include "userprog/pagedir.h"
#include "filesys/file.h"
#include "userprog/syscall.h"
#include "vm/page.h"
#include "vm/swap.h"

static struct list frame_list;
static struct lock frame_lock;
static struct list_elem *clock_hand;

static void *frame_evict (void);

void
frame_system_init (void)
{
  list_init (&frame_list);
  lock_init (&frame_lock);
  clock_hand = NULL;
}

void *
frame_alloc (enum palloc_flags flags, struct spt_entry *spte)
{
  void *kpage = palloc_get_page (flags);
  
  if (kpage == NULL)
    {
      kpage = frame_evict ();
      if (kpage == NULL)
        PANIC ("Frame allocation failed after eviction");
    }

  struct phys_frame *f = malloc (sizeof (struct phys_frame));
  if (f == NULL)
    {
       palloc_free_page (kpage);
       return NULL;
    }

  f->kpage = kpage;
  f->spte = spte;
  f->thread = thread_current ();
  
  lock_acquire (&frame_lock);
  list_push_back (&frame_list, &f->elem);
  lock_release (&frame_lock);

  return kpage;
}

void
frame_free (void *kpage)
{
  struct list_elem *e;

  lock_acquire (&frame_lock);
  for (e = list_begin (&frame_list); e != list_end (&frame_list); e = list_next (e))
    {
      struct phys_frame *f = list_entry (e, struct phys_frame, elem);
      if (f->kpage == kpage)
        {
          if (clock_hand == e)
            {
               clock_hand = list_next (clock_hand);
               if (clock_hand == list_end (&frame_list))
                 clock_hand = list_begin (&frame_list);
            }
          list_remove (e);
          free (f);
          palloc_free_page (kpage);
          break;
        }
    }
  lock_release (&frame_lock);
}

void
frame_cleanup (struct thread *t)
{
  struct list_elem *e;
  struct list_elem *next;

  lock_acquire (&frame_lock);
  for (e = list_begin (&frame_list); e != list_end (&frame_list); e = next)
    {
      next = list_next (e);
      struct phys_frame *f = list_entry (e, struct phys_frame, elem);
      if (f->thread == t)
        {
           if (clock_hand == e)
             {
                clock_hand = list_next (e);
                if (clock_hand == list_end (&frame_list))
                   clock_hand = list_begin (&frame_list);
             }
           list_remove (e);
           void *kpage = f->kpage;
           free (f);
           palloc_free_page (kpage);
        }
    }
  lock_release (&frame_lock);
}

static void *
frame_evict (void)
{
  lock_acquire (&frame_lock);
  
  if (list_empty (&frame_list))
    {
      lock_release (&frame_lock);
      return NULL; 
    }

  if (clock_hand == NULL || clock_hand == list_end (&frame_list))
    clock_hand = list_begin (&frame_list);

  while (true)
    {
      struct list_elem *e = clock_hand;
      struct phys_frame *f = list_entry (e, struct phys_frame, elem);
      
      clock_hand = list_next (clock_hand);
      if (clock_hand == list_end (&frame_list))
        clock_hand = list_begin (&frame_list);

      if (f->spte == NULL || f->thread == NULL)
        continue;
      
      if (f->thread->pagedir == NULL) 
        continue;

      if (pagedir_is_accessed (f->thread->pagedir, f->spte->uva))
        {
          pagedir_set_accessed (f->thread->pagedir, f->spte->uva, false);
          continue;
        }

      struct spt_entry *spte = f->spte;
      
      if (spte->type == SPT_FILE)
        {
           if (pagedir_is_dirty (f->thread->pagedir, spte->uva))
             {
                lock_release (&frame_lock);
                
                struct lock *fs_lock = get_fs_lock ();
                bool locked = lock_held_by_current_thread (fs_lock);
                if (!locked)
                  lock_acquire (fs_lock);
                  
                file_write_at (spte->file, f->kpage, spte->read_bytes, spte->offset);
                
                if (!locked)
                  lock_release (fs_lock);
                  
                lock_acquire (&frame_lock);
             }
        }
      else if (spte->type == SPT_SWAP || spte->type == SPT_ANON || (spte->type == SPT_BIN && spte->writable)) 
        {
           spte->type = SPT_SWAP;
           spte->swap_slot = swap_out_page (f->kpage);
        }

      spte->is_loaded = false;
      pagedir_clear_page (f->thread->pagedir, spte->uva);
      
      list_remove (e);
      void *kpage = f->kpage;
      free (f);
      
      lock_release (&frame_lock);
      return kpage;
    }
}
