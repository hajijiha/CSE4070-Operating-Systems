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
#include "threads/vaddr.h"

#define MAX_ARGS 64
#define WORD_SIZE 4

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);

tid_t
process_execute (const char *file_name)
{
  char *fn_copy;
  tid_t tid;
  char thread_name[16];
  struct thread *child;

  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);

  char temp_name[16];
  strlcpy(temp_name, file_name, sizeof(temp_name));
  char *save_ptr;
  strtok_r(temp_name, " ", &save_ptr);
  strlcpy(thread_name, temp_name, sizeof(thread_name));

  tid = thread_create (thread_name, PRI_DEFAULT, start_process, fn_copy);

  if (tid == TID_ERROR)
  {
    palloc_free_page (fn_copy);
    return tid;
  }

  child = get_child_thread (tid);
  if (child == NULL)
    return TID_ERROR;

  sema_down (&child->load_sema);

  if (!child->load_success)
    return TID_ERROR;

  return tid;
}

int process_add_file(struct file* f){
  struct thread* t = thread_current();
  int i;
  for(i=3;i<FDTABLE_SIZE;i++){
    if(t->fd_table[i]==NULL){
      t->fd_table[i]=f;
      return i;
    }
  }
  return -1;
}

struct file* process_get_file(int fd){
  struct thread* t = thread_current();
  if(fd<3 || fd>=FDTABLE_SIZE){
    return NULL;
  }
  return t->fd_table[fd];
}

void process_close_file(int fd){
  struct thread* t = thread_current();
  if(fd<3 || fd>=FDTABLE_SIZE){
    return;
  }
  if(t->fd_table[fd]!=NULL){
    file_close(t->fd_table[fd]);
    t->fd_table[fd]=NULL;
  }
}

static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;
  struct thread *t = thread_current();
  
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (file_name, &if_.eip, &if_.esp);

  t->load_success = success;
  sema_up (&t->load_sema);

  palloc_free_page (file_name);

  if (!success)
      sys_exit(-1);
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

int
process_wait (tid_t child_tid)
{
  struct thread *child = get_child_thread (child_tid);
  int exit_status;

  if (child == NULL)
    return -1;

  if (child->waited_on)
    return -1;

  child->waited_on = true;

  sema_down (&child->exit_sema);

  exit_status = child->exit_status;
  list_remove (&child->child_elem);

  palloc_free_page(child);

  return exit_status;
}

void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;
  for(int i=3;i<FDTABLE_SIZE;i++){
    process_close_file(i);
  }
  if (cur->executable != NULL)
  {
    file_allow_write(cur->executable);
    file_close(cur->executable);
    cur->executable = NULL;
  }

  pd = cur->pagedir;
  if (pd != NULL)
    {
      cur->saved_pd = pd;
      cur->pagedir = NULL;
      pagedir_activate (NULL);
    }
}

void
process_activate (void)
{
  struct thread *t = thread_current ();

  pagedir_activate (t->pagedir);

  tss_update ();
}

bool construct_stack(const char *cmd_line, void **esp) {
  char *cmd_copy = palloc_get_page(0);
  if (cmd_copy == NULL)
    return false;
  strlcpy(cmd_copy, cmd_line, PGSIZE);

  char *arg_addresses[MAX_ARGS];
  int argc = 0;
  char *token, *save_ptr;

  for (token = strtok_r(cmd_copy, " ", &save_ptr); token != NULL;
       token = strtok_r(NULL, " ", &save_ptr))
  {
      if (argc >= MAX_ARGS) break;

      int len = strlen(token) + 1;
      *esp -= len;
      
      memcpy(*esp, token, len);

      arg_addresses[argc] = *esp;
      argc++;
  }

  int padding = (uintptr_t)*esp % WORD_SIZE;
  if (padding != 0) {
      *esp -= padding;
      memset(*esp, 0, padding);
  }
  
  *esp -= sizeof(char *);
  *((char **)*esp) = NULL;

  for (int i = argc - 1; i >= 0; i--) {
      *esp -= sizeof(char *);
      *((char **)*esp) = arg_addresses[i];
  }

  char **argv_ptr = *esp;
  *esp -= sizeof(char **);
  *((char ***)*esp) = argv_ptr;

  *esp -= sizeof(int);
  *((int *)*esp) = argc;

  *esp -= sizeof(void *);
  *((void **)*esp) = NULL;

  palloc_free_page(cmd_copy);
  return true;
}


typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

#define PE32Wx PRIx32 
#define PE32Ax PRIx32 
#define PE32Ox PRIx32 
#define PE32Hx PRIx16 

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

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

bool
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
  strlcpy(file_name_copy, file_name, sizeof file_name_copy);
  
  char *save_ptr;
  char *parsed_file_name = strtok_r(file_name_copy, " ", &save_ptr);
  
  file = filesys_open (parsed_file_name);
  if (file == NULL)
    {
      printf ("load: %s: open failed\n", parsed_file_name);
      goto done;
    }

  t->executable = file;
  file_deny_write(file);

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
  
  if (!setup_stack (esp))
    goto done;

  if (!construct_stack(file_name, esp))
    goto done;

  *eip = (void (*) (void)) ehdr.e_entry;
  
  success = true;

 done:
  if (!success)
  {
    if (t->pagedir != NULL)
    {
      pagedir_activate(NULL);
      pagedir_destroy(t->pagedir);
      t->pagedir = NULL;
    }
    if (file != NULL)
    {
      file_close(file);
      t->executable = NULL;
    }
  }
  return success;
}

static bool install_page (void *upage, void *kpage, bool writable);

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

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      if (!install_page (upage, kpage, writable)) 
        {
          palloc_free_page (kpage);
          return false; 
        }

      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

static bool
setup_stack (void **esp) 
{
  uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
        *esp = PHYS_BASE;
      else
        palloc_free_page (kpage);
    }
  return success;
}

static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}