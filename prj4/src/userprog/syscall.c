#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "filesys/file.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "vm/page.h"
#include "vm/frame.h"
#include <string.h>
#include "threads/malloc.h"

static void syscall_handler (struct intr_frame *);
static struct lock file_system_lock;

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init (&file_system_lock);
}

struct lock*
get_fs_lock (void)
{
  return &file_system_lock;
}

static void
check_valid_ptr (const void *vaddr)
{
  if (!is_user_vaddr (vaddr) || vaddr == NULL)
    {
      if (lock_held_by_current_thread (&file_system_lock))
        lock_release (&file_system_lock);
      sys_exit (-1);
    }
}

static void
check_valid_buffer (const void *buffer, unsigned size)
{
  char *ptr = (char *) buffer;
  unsigned i;
  for (i = 0; i < size; i++)
    {
       check_valid_ptr ((const void *) ptr);
       ptr++;
    }
}

static void
check_valid_string (const char *str)
{
  check_valid_ptr ((const void *) str);
  while (*str != 0)
    {
       str++;
       check_valid_ptr ((const void *) str);
    }
}

static void
preload_buffer (void *buffer, unsigned size, bool write)
{
  char *ptr = (char *) buffer;
  unsigned i;
  for (i = 0; i < size; i += PGSIZE)
    {
       volatile char c = ptr[i];
       if (write)
         ptr[i] = c;
    }
  // Touch last byte too
  if (size > 0) {
      volatile char c = ptr[size-1];
      if (write)
        ptr[size-1] = c;
  }
}

static void
handler_halt (struct intr_frame *f UNUSED)
{
  shutdown_power_off ();
}

static void
handler_exit (struct intr_frame *f)
{
  check_valid_buffer (f->esp + 4, 4);
  int status = *(int *) (f->esp + 4);
  sys_exit (status);
}

static void
handler_exec (struct intr_frame *f)
{
  check_valid_buffer (f->esp + 4, 4);
  char *cmd = *(char **) (f->esp + 4);
  check_valid_string (cmd);
  f->eax = process_execute (cmd);
}

static void
handler_wait (struct intr_frame *f)
{
  check_valid_buffer (f->esp + 4, 4);
  int tid = *(int *) (f->esp + 4);
  f->eax = process_wait (tid);
}

static void
handler_create (struct intr_frame *f)
{
  check_valid_buffer (f->esp + 4, 4);
  check_valid_buffer (f->esp + 8, 4);
  char *file = *(char **) (f->esp + 4);
  unsigned initial_size = *(unsigned *) (f->esp + 8);
  check_valid_string (file);
  
  lock_acquire (&file_system_lock);
  f->eax = filesys_create (file, initial_size);
  lock_release (&file_system_lock);
}

static void
handler_remove (struct intr_frame *f)
{
  check_valid_buffer (f->esp + 4, 4);
  char *file = *(char **) (f->esp + 4);
  check_valid_string (file);
  
  lock_acquire (&file_system_lock);
  f->eax = filesys_remove (file);
  lock_release (&file_system_lock);
}

static void
handler_open (struct intr_frame *f)
{
  check_valid_buffer (f->esp + 4, 4);
  char *file = *(char **) (f->esp + 4);
  check_valid_string (file);
  
  lock_acquire (&file_system_lock);
  struct file *o_file = filesys_open (file);
  if (o_file == NULL)
    {
       lock_release (&file_system_lock);
       f->eax = -1;
    }
  else
    {
       int fd = process_add_file (o_file);
       if (fd == -1)
         file_close (o_file);
       lock_release (&file_system_lock);
       f->eax = fd;
    }
}

static void
handler_filesize (struct intr_frame *f)
{
  check_valid_buffer (f->esp + 4, 4);
  int fd = *(int *) (f->esp + 4);
  
  lock_acquire (&file_system_lock);
  struct file *file = process_get_file (fd);
  if (file == NULL)
    {
      lock_release (&file_system_lock);
      f->eax = -1;
    }
  else
    {
      f->eax = file_length (file);
      lock_release (&file_system_lock);
    }
}

static void
handler_read (struct intr_frame *f)
{
  check_valid_buffer (f->esp + 4, 4);
  check_valid_buffer (f->esp + 8, 4);
  check_valid_buffer (f->esp + 12, 4);
  int fd = *(int *) (f->esp + 4);
  void *buffer = *(void **) (f->esp + 8);
  unsigned size = *(unsigned *) (f->esp + 12);
  
  check_valid_buffer (buffer, size);
  
  lock_acquire (&file_system_lock);
  if (fd == 0)
    {
      unsigned i;
      for (i = 0; i < size; i++)
        ((uint8_t *)buffer)[i] = input_getc ();
      f->eax = size;
      lock_release (&file_system_lock);
    }
  else
    {
       struct file *file = process_get_file (fd);
       if (file == NULL)
         {
            lock_release (&file_system_lock);
            f->eax = -1;
         }
       else
         {
            preload_buffer (buffer, size, true);
            f->eax = file_read (file, buffer, size);
            lock_release (&file_system_lock);
         }
    }
}

static void
handler_write (struct intr_frame *f)
{
  check_valid_buffer (f->esp + 4, 4);
  check_valid_buffer (f->esp + 8, 4);
  check_valid_buffer (f->esp + 12, 4);
  int fd = *(int *) (f->esp + 4);
  void *buffer = *(void **) (f->esp + 8);
  unsigned size = *(unsigned *) (f->esp + 12);
  
  check_valid_buffer (buffer, size);
  
  lock_acquire (&file_system_lock);
  if (fd == 1)
    {
       preload_buffer (buffer, size, false);
       putbuf (buffer, size);
       f->eax = size;
       lock_release (&file_system_lock);
    }
  else
    {
       struct file *file = process_get_file (fd);
       if (file == NULL)
         {
            lock_release (&file_system_lock);
            f->eax = 0;
         }
       else
         {
            preload_buffer (buffer, size, false);
            f->eax = file_write (file, buffer, size);
            lock_release (&file_system_lock);
         }
    }
}

static void
handler_seek (struct intr_frame *f)
{
  check_valid_buffer (f->esp + 4, 4);
  check_valid_buffer (f->esp + 8, 4);
  int fd = *(int *) (f->esp + 4);
  unsigned position = *(unsigned *) (f->esp + 8);
  
  lock_acquire (&file_system_lock);
  struct file *file = process_get_file (fd);
  if (file != NULL)
    file_seek (file, position);
  lock_release (&file_system_lock);
}

static void
handler_tell (struct intr_frame *f)
{
  check_valid_buffer (f->esp + 4, 4);
  int fd = *(int *) (f->esp + 4);
  
  lock_acquire (&file_system_lock);
  struct file *file = process_get_file (fd);
  if (file != NULL)
    f->eax = file_tell (file);
  else
    f->eax = -1;
  lock_release (&file_system_lock);
}

static void
handler_close (struct intr_frame *f)
{
  check_valid_buffer (f->esp + 4, 4);
  int fd = *(int *) (f->esp + 4);
  
  lock_acquire (&file_system_lock);
  process_close_file (fd);
  lock_release (&file_system_lock);
}

static void
handler_mmap (struct intr_frame *f)
{
  check_valid_buffer (f->esp + 4, 4);
  check_valid_buffer (f->esp + 8, 4);
  int fd = *(int *) (f->esp + 4);
  void *addr = *(void **) (f->esp + 8);
  
  if (addr == NULL || pg_ofs (addr) != 0 || !is_user_vaddr (addr) || fd < 2)
    {
       f->eax = -1;
       return;
    }
    
  struct thread *t = thread_current ();
  struct file *file = NULL;
  
  lock_acquire (&file_system_lock);
  struct file *orig_file = process_get_file (fd);
  if (orig_file != NULL)
     file = file_reopen (orig_file);
  lock_release (&file_system_lock);
  
  if (file == NULL)
    {
       f->eax = -1;
       return;
    }
  
  off_t length = file_length (file);
  if (length == 0)
    {
       lock_acquire (&file_system_lock);
       file_close (file);
       lock_release (&file_system_lock);
       f->eax = -1;
       return;
    }
    
  off_t ofs;
  for (ofs = 0; ofs < length; ofs += PGSIZE)
    {
       if (spt_find (&t->spt, addr + ofs) != NULL)
         {
            lock_acquire (&file_system_lock);
            file_close (file);
            lock_release (&file_system_lock);
            f->eax = -1;
            return;
         }
    }
    
   for (ofs = 0; ofs < length; ofs += PGSIZE)
     {
        struct spt_entry *spte = malloc (sizeof (struct spt_entry));
        if (spte == NULL)
          {
             // Cleanup already allocated entries? 
             // Ideally we should rollback, but for now just fail new ones.
             // The process will eventually exit and clean up.
             // But we need to close 'file'.
             lock_acquire (&file_system_lock);
             file_close (file);
             lock_release (&file_system_lock);
             f->eax = -1;
             return;
          }
          
        uint32_t read_bytes = (ofs + PGSIZE < length) ? PGSIZE : (uint32_t) (length - ofs);
        uint32_t zero_bytes = PGSIZE - read_bytes;
        
        spte->uva = addr + ofs;
        spte->type = SPT_FILE;
        spte->writable = true;
        spte->is_loaded = false;
        
        lock_acquire (&file_system_lock);
        spte->file = file_reopen (file);
        lock_release (&file_system_lock);
        
        spte->offset = ofs;
        spte->read_bytes = read_bytes;
        spte->zero_bytes = zero_bytes;
        
        spt_insert (&t->spt, spte);
     }
     
   lock_acquire (&file_system_lock);
   file_close (file);
   lock_release (&file_system_lock);
     
   f->eax = (mapid_t) addr;
}

static void
handler_munmap (struct intr_frame *f)
{
  check_valid_buffer (f->esp + 4, 4);
  mapid_t mapping = *(mapid_t *) (f->esp + 4);
  sys_munmap (mapping);
}

void
sys_exit (int status)
{
  struct thread *cur = thread_current ();
  cur->exit_status = status;
  printf ("%s: exit(%d)\n", cur->name, status);
  thread_exit ();
}

void
sys_munmap (mapid_t mapping)
{
  struct thread *t = thread_current ();
  void *addr = (void *) mapping;
  struct spt_entry *spte;
  
  while ((spte = spt_find (&t->spt, addr)) != NULL)
    {
       if (spte->type != SPT_FILE)
         break;
         
       if (spte->is_loaded)
         {
            if (pagedir_is_dirty (t->pagedir, spte->uva))
              {
                 lock_acquire (&file_system_lock);
                 file_write_at (spte->file, spte->kpage, spte->read_bytes, spte->offset);
                 lock_release (&file_system_lock);
              }
            frame_free (spte->kpage);
            pagedir_clear_page (t->pagedir, spte->uva);
         }
       
       struct hash_elem *e = hash_delete (&t->spt, &spte->elem);
       
       lock_acquire (&file_system_lock);
       if (spte->type == SPT_FILE) 
         file_close (spte->file);
       lock_release (&file_system_lock);
       
       free (spte);
       addr += PGSIZE;
    }
}

static void
syscall_handler (struct intr_frame *f)
{
  check_valid_ptr ((const void *) f->esp);
  
  thread_current ()->user_esp = f->esp;

  int sys_code = *(int *) f->esp;
  
  switch (sys_code)
    {
    case SYS_HALT:
      handler_halt (f);
      break;
    case SYS_EXIT:
      handler_exit (f);
      break;
    case SYS_EXEC:
      handler_exec (f);
      break;
    case SYS_WAIT:
      handler_wait (f);
      break;
    case SYS_CREATE:
      handler_create (f);
      break;
    case SYS_REMOVE:
      handler_remove (f);
      break;
    case SYS_OPEN:
      handler_open (f);
      break;
    case SYS_FILESIZE:
      handler_filesize (f);
      break;
    case SYS_READ:
      handler_read (f);
      break;
    case SYS_WRITE:
      handler_write (f);
      break;
    case SYS_SEEK:
      handler_seek (f);
      break;
    case SYS_TELL:
      handler_tell (f);
      break;
    case SYS_CLOSE:
      handler_close (f);
      break;
    case SYS_MMAP:
      handler_mmap (f);
      break;
    case SYS_MUNMAP:
      handler_munmap (f);
      break;
    default:
      sys_exit (-1);
    }
}