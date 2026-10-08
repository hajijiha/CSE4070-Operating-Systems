#include "vm/page.h"
#include <stdio.h>
#include <stdlib.h>
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "filesys/file.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include "userprog/syscall.h"
#include <string.h>

static unsigned spt_hash_func (const struct hash_elem *e, void *aux UNUSED);
static bool spt_less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED);
static void spt_destroy_func (struct hash_elem *e, void *aux UNUSED);

void
spt_init (struct hash *spt)
{
  hash_init (spt, spt_hash_func, spt_less_func, NULL);
}

void
spt_destroy (struct hash *spt)
{
  hash_destroy (spt, spt_destroy_func);
}

struct spt_entry *
spt_find (struct hash *spt, void *uva)
{
  struct spt_entry p;
  struct hash_elem *e;

  p.uva = pg_round_down (uva);
  e = hash_find (spt, &p.elem);
  return e != NULL ? hash_entry (e, struct spt_entry, elem) : NULL;
}

bool
spt_insert (struct hash *spt, struct spt_entry *p)
{
  if (hash_insert (spt, &p->elem) == NULL)
    return true;
  return false;
}

bool
spt_load (struct spt_entry *p)
{
  if (p->is_loaded)
    return true;

  void *kpage = frame_alloc (PAL_USER | PAL_ZERO, p);
  if (kpage == NULL) {
    printf ("Debug: Frame alloc failed\n");
    return false;
  }

  bool success = false;

  switch (p->type)
    {
    case SPT_BIN:
    case SPT_FILE:
      if (p->read_bytes > 0)
        {
           struct lock *fs_lock = get_fs_lock ();
           bool locked = lock_held_by_current_thread (fs_lock);
           if (!locked)
             lock_acquire (fs_lock);
             
           off_t read_bytes = file_read_at (p->file, kpage, p->read_bytes, p->offset);
           
           if (!locked)
             lock_release (fs_lock);
             
           if (read_bytes != (off_t) p->read_bytes)
             {
               printf ("Debug: file_read_at failed: req %d vs read %d\n", p->read_bytes, (int)read_bytes);
               frame_free (kpage);
               return false;
             }
        }
      memset (kpage + p->read_bytes, 0, p->zero_bytes);
      success = true;
      break;

    case SPT_ANON: 
      success = true;
      break;

    case SPT_SWAP:
      swap_in_page (p->swap_slot, kpage);
      success = true;
      break;

    default:
      success = false;
    }

  if (success)
    {
       if (!pagedir_set_page (thread_current ()->pagedir, p->uva, kpage, p->writable))
         {
           frame_free (kpage);
           return false;
         }
       p->is_loaded = true;
       p->kpage = kpage;
    }
  else
    {
       frame_free (kpage);
    }
  
  return success;
}

static unsigned
spt_hash_func (const struct hash_elem *e, void *aux UNUSED)
{
  const struct spt_entry *p = hash_entry (e, struct spt_entry, elem);
  return hash_bytes (&p->uva, sizeof p->uva);
}

static bool
spt_less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED)
{
  const struct spt_entry *pa = hash_entry (a, struct spt_entry, elem);
  const struct spt_entry *pb = hash_entry (b, struct spt_entry, elem);
  return pa->uva < pb->uva;
}

static void
spt_destroy_func (struct hash_elem *e, void *aux UNUSED)
{
  struct spt_entry *p = hash_entry (e, struct spt_entry, elem);
  if (p->is_loaded)
    {
       frame_free (p->kpage);
       pagedir_clear_page (thread_current ()->pagedir, p->uva);
    }
  if (p->type == SPT_SWAP)
    {
       swap_free_slot (p->swap_slot);
    }
  if ((p->type == SPT_FILE || p->type == SPT_BIN) && p->file != NULL)
    {
       // Note: We need lock here? 
       // Usually destroying SPT happens at exit where single threaded, 
       // but fs_lock is needed for file_close implementation safety?
       // Let's assume we need it or should get it.
       // But wait, spt_destroy is called from process_exit.
       // process_exit doesn't hold fs_lock when calling spt_destroy.
       // So we should acquire it.
       // But if we are in exception context? No, process exit.
       struct lock *fs_lock = get_fs_lock (); 
       // Need to be careful about not re-acquiring if held?
       // process_exit shouldn't hold it. 
       // But let's check lock_held to be safe.
       if (!lock_held_by_current_thread (fs_lock))
         lock_acquire (fs_lock);
       file_close (p->file);
       if (lock_held_by_current_thread (fs_lock))
         lock_release (fs_lock);
    }
  free (p);
}
