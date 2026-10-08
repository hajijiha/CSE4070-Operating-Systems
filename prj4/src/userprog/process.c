#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "vm/page.h"
#include "vm/frame.h"
#include "userprog/syscall.h"

struct start_params
  {
    char *file_name;
    struct semaphore *sema;
    bool success;
  };

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);

tid_t
process_execute (const char *file_name)
{
  char *fn_copy;
  tid_t tid;
  struct start_params params;
  struct semaphore load_sema;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);

  char temp_name[16];
  strlcpy (temp_name, file_name, sizeof (temp_name));
  char *save_ptr;
  strtok_r (temp_name, " ", &save_ptr);

  sema_init (&load_sema, 0);
  params.file_name = fn_copy;
  params.sema = &load_sema;
  params.success = false;

  tid = thread_create (temp_name, PRI_DEFAULT, start_process, &params);
  
  if (tid == TID_ERROR)
    {
       palloc_free_page (fn_copy);
    }
  else
    {
       sema_down (&load_sema);
       if (!params.success)
         tid = TID_ERROR;
    }

  return tid;
}

static void
start_process (void *aux)
{
  struct start_params *params = aux;
  char *file_name = params->file_name;
  struct intr_frame if_;
  bool success;
  struct thread *t = thread_current ();
  
  spt_init (&t->spt);

  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (file_name, &if_.eip, &if_.esp);

  palloc_free_page (file_name);
  
  params->success = success;
  sema_up (params->sema);
  
  if (!success)
    sys_exit (-1);

  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

int
process_wait (tid_t child_tid UNUSED)
{
  struct thread *child = get_child_thread (child_tid);
  if (child == NULL)
    return -1;
    
  if (child->waited_on)
    return -1;
    
  child->waited_on = true;
  
  if (child->status != THREAD_DYING)
    sema_down (&child->exit_sema);
    
  int status = child->exit_status;
  list_remove (&child->child_elem);
  
  // Potential memory leak: child struct not freed to avoid race.
  // Allpass reference often handles this via complex synchronization or weak references.
  
  return status;
}

void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;
  
  struct lock *fs_lock = get_fs_lock ();
  
  if (!lock_held_by_current_thread (fs_lock))
    lock_acquire (fs_lock);
    
  if (cur->executable)
    {
       file_close (cur->executable);
    }
  
  int i;
  for (i = 0; i < FDTABLE_SIZE; i++)
    {
       if (cur->fd_table[i] != NULL)
         {
            file_close (cur->fd_table[i]);
            cur->fd_table[i] = NULL;
         }
    }
  lock_release (fs_lock);
  
  pd = cur->pagedir;
  if (pd != NULL)
    {
       struct hash_iterator it;
       struct lock *fsl = get_fs_lock ();
       
       hash_first (&it, &cur->spt);
       while (hash_next (&it))
         {
            struct spt_entry *spte = hash_entry (hash_cur (&it), struct spt_entry, elem);
            if (spte->type == SPT_FILE && spte->kpage != NULL)
              {
                 if (pagedir_is_dirty (cur->pagedir, spte->uva))
                   {
                      lock_acquire (fsl);
                      file_write_at (spte->file, spte->kpage, spte->read_bytes, spte->offset);
                      lock_release (fsl);
                   }
              }
         }
         
         // Redundant loop removed (handled in spt_destroy)
         
       frame_cleanup (cur);
       spt_destroy (&cur->spt);
       
       cur->pagedir = NULL;
       pagedir_activate (NULL);
       pagedir_destroy (pd);
    }
}

void
process_activate (void)
{
  struct thread *t = thread_current ();
  pagedir_activate (t->pagedir);
  tss_update ();
}

int process_add_file (struct file* f) {
  struct thread* t = thread_current ();
  int i;
  for (i = 3; i < FDTABLE_SIZE; i++) {
    if (t->fd_table[i] == NULL) {
      t->fd_table[i] = f;
      return i;
    }
  }
  return -1;
}

struct file* process_get_file (int fd) {
  struct thread* t = thread_current ();
  if (fd < 3 || fd >= FDTABLE_SIZE) return NULL;
  return t->fd_table[fd];
}

void process_close_file (int fd) {
  struct thread* t = thread_current ();
  if (fd < 3 || fd >= FDTABLE_SIZE) return;
  if (t->fd_table[fd] != NULL) {
    file_close (t->fd_table[fd]);
    t->fd_table[fd] = NULL;
  }
}

typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

#define PT_NULL    0
#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_INTERP  3
#define PT_NOTE    4
#define PT_SHLIB   5
#define PT_PHDR    6
#define PT_STACK   0x6474e551

#define PF_X 1
#define PF_W 2
#define PF_R 4

static bool setup_stack (void **esp, const char *file_name);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

static bool
load (const char *file_name, void (**eip) (void), void **esp)
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL)
    goto done;
  process_activate ();
  
  char file_name_copy[256];
  strlcpy (file_name_copy, file_name, sizeof file_name_copy);
  char *save_ptr;
  char *prog_name = strtok_r (file_name_copy, " ", &save_ptr);

  lock_acquire (get_fs_lock ());
  file = filesys_open (prog_name);
  if (file == NULL)
    {
      lock_release (get_fs_lock ());
      printf ("load: %s: open failed\n", file_name);
      goto done;
    }
    
  t->executable = file;
  file_deny_write (file);
  lock_release (get_fs_lock ());

  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024)
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done;
    }

  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++)
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type)
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file))
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else
                {
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  if (!setup_stack (esp, file_name))
    goto done;

  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  if (!success)
    {
       if (t->pagedir != NULL)
         {
            pagedir_activate (NULL);
            pagedir_destroy (t->pagedir);
            t->pagedir = NULL;
         }
       if (file != NULL)
         {
            lock_acquire (get_fs_lock ());
            file_close (file);
            lock_release (get_fs_lock ());
            t->executable = NULL;
         }
    }
  return success;
}

static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file)
{
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
    return false;

  if (phdr->p_offset > (Elf32_Off) file_length (file))
    return false;

  if (phdr->p_memsz < phdr->p_filesz)
    return false;

  if (phdr->p_memsz == 0)
    return false;

  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  if (phdr->p_vaddr < PGSIZE)
    return false;

  return true;
}

static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  while (read_bytes > 0 || zero_bytes > 0)
    {
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      struct spt_entry *spte = malloc (sizeof (struct spt_entry));
      if (spte == NULL)
        return false;
        
      spte->uva = upage;
      
      lock_acquire (get_fs_lock ());
      spte->file = file_reopen (file);
      lock_release (get_fs_lock ());
      
      if (spte->file == NULL)
        {
           free (spte);
           return false;
        }
      
      spte->offset = ofs;
      spte->read_bytes = page_read_bytes;
      spte->zero_bytes = page_zero_bytes;
      spte->writable = writable;
      spte->type = SPT_BIN;
      spte->is_loaded = false;
      
      if (!spt_insert (&thread_current ()->spt, spte))
        {
           free (spte);
           return false;
        }

      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
      ofs += page_read_bytes;
    }
  return true;
}

static bool
setup_stack (void **esp, const char *file_name)
{
  bool success = false;
  struct spt_entry *spte = malloc (sizeof (struct spt_entry));
  if (spte == NULL)
    return false;
    
  spte->uva = ((uint8_t *) PHYS_BASE) - PGSIZE;
  spte->type = SPT_ANON;
  spte->writable = true;
  spte->is_loaded = true;
  spte->file = NULL;
  
  void *kpage = frame_alloc (PAL_USER | PAL_ZERO, spte);
  if (kpage != NULL)
    {
       success = pagedir_set_page (thread_current ()->pagedir, spte->uva, kpage, true);
       if (success) 
         {
            spte->kpage = kpage;
            spt_insert (&thread_current ()->spt, spte);
            *esp = PHYS_BASE;
            
            char *token;
            char *save_ptr;
            int argc = 0;
            char *argv[64]; // Max args assumption
            
            // Assume file_name is mutable format or we copy it to kpage first?
            // file_name pointer is in kernel memory (passed from start_process).
            // We need to put strings on USER stack.
            // Copy file_name to user stack?
            // Actually, we should parse 'file_name' and push tokens.
            // The file_name string is in kernel.
            // Let's copy it to a temporary buffer or handle it.
            // Be careful not to overflow stack.
            
            // Re-copy file_name because strtok modifies it.
            // We use the kpage directly if we want, but easier to use a copy.
            // However, we need to push strings to *top* of stack.
            
            // First, push strings.
            // We need to parse first to know offsets? No, just push and save pointers.
            
            char *cmd_copy = palloc_get_page(0);
            if (cmd_copy) {
                strlcpy(cmd_copy, file_name, PGSIZE);
                
                for (token = strtok_r (cmd_copy, " ", &save_ptr); token != NULL;
                     token = strtok_r (NULL, " ", &save_ptr))
                  {
                    *esp -= strlen (token) + 1;
                    memcpy (*esp, token, strlen (token) + 1);
                    argv[argc++] = *esp;
                  }
                palloc_free_page(cmd_copy);
                
                // Word align
                while ((uintptr_t) *esp % 4 != 0)
                   {
                      *esp -= 1;
                      *(uint8_t *) *esp = 0;
                   }
                   
                // Push argv pointers (including null)
                *esp -= 4; // Null sentinel
                *(char **) *esp = NULL;
                
                int i;
                for (i = argc - 1; i >= 0; i--)
                  {
                     *esp -= 4;
                     *(char **) *esp = argv[i];
                  }
                  
                char **base_argv = *esp;
                
                // Push argv
                *esp -= 4;
                *(char ***) *esp = base_argv;
                
                // Push argc
                *esp -= 4;
                *(int *) *esp = argc;
                
                // Push ret addr
                *esp -= 4;
                *(void **) *esp = NULL;
            } else {
                success = false; // OOM
            }
         }
       else
         {
            frame_free (kpage);
            free (spte);
         }
    }
  else
    {
       free (spte);
    }
  return success;
}
