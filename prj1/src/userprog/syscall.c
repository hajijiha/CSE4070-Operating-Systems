#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "filesys/file.h"
#include "userprog/process.h"
#include "threads/vaddr.h"
#include "devices/input.h"
#include "devices/shutdown.h"
#include "userprog/pagedir.h"

void syscall_handler (struct intr_frame *);
void sys_halt (void);
void sys_exit (int status);
int sys_exec (const char *cmd_line);
int sys_wait (int pid);
int sys_read (int fd, void *buffer, unsigned size);
int sys_write (int fd, const void *buffer, unsigned size);
int sys_fibonacci(int n);
int sys_max_of_four_int(int a, int b, int c, int d);

#define ARG_INT(n)     (*(int32_t *)(f->esp + 4 * (n)))
#define ARG_PTR(n)     (*(void **)(f->esp + 4 * (n)))

static void
check_user_vaddr(const void* vaddr) {
  if (vaddr == NULL || !is_user_vaddr(vaddr)) {
      sys_exit(-1);
  }
  if (pagedir_get_page(thread_current()->pagedir, vaddr) == NULL) {
      sys_exit(-1);
  }
}
static void
check_user_buffer (const void *vaddr, unsigned size)
{
  if (vaddr == NULL) sys_exit(-1);

  char *ptr = (char *) vaddr;
  for (unsigned i = 0; i < size; i++)
    {
     
      check_user_vaddr((const void *)(ptr + i));
    }
}
static void
check_user_string (const char *str)
{
  if (str == NULL) sys_exit(-1);

  check_user_vaddr((const void *)str);
  while (*str != '\0')
    {
      str++;
      check_user_vaddr((const void *)str);
    }
}


void 
sys_halt (void) {
  shutdown_power_off();
}

void
sys_exit (int status)
{
  struct thread *t = thread_current();

  printf("%s: exit(%d)\n", t->name, status);

  t->exit_status = status;

  thread_exit();
}

int 
sys_exec (const char *cmd_line) {
  return process_execute(cmd_line);
}

int 
sys_wait (int pid) {
  return process_wait(pid);
}

int 
sys_read (int fd, void *buffer, unsigned size) {
  unsigned int bytes_read = 0;
  
  if (fd == 0) {
    for (bytes_read = 0; bytes_read < size; bytes_read++) {
      uint8_t temp = input_getc();
      *(uint8_t *)(buffer + bytes_read) = temp;
    }
    return bytes_read;
  }
  
  return -1;
}

int 
sys_write (int fd, const void *buffer, unsigned size) {
  if (fd == 1) {
    putbuf(buffer, size);
    return size;
  }
  
  return -1;
}

int 
sys_fibonacci(int n) {
    if (n < 0) {
        return -1;
    }
    if (n <= 1) {
        return n;
    }
    
    int a = 0;
    int b = 1;
    int result = 0;
    
    for (int i = 2; i <= n; i++) {
        result = a + b;
        a = b;
        b = result;
    }
    return result;
}

int 
sys_max_of_four_int(int a, int b, int c, int d) {
    int max_val = a;
    
    if (b > max_val) {
        max_val = b;
    }
    if (c > max_val) {
        max_val = c;
    }
    if (d > max_val) {
        max_val = d;
    }
    
    return max_val;
}

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

void
syscall_handler (struct intr_frame *f)
{
    if (f->esp == NULL || !is_user_vaddr(f->esp) || !is_user_vaddr(f->esp + 3)) {
        sys_exit(-1);
    }
    check_user_vaddr(f->esp);

    int syscall_num = *(int32_t *)f->esp;
    
    switch (syscall_num) 
    {
        case SYS_HALT: 
        {
            sys_halt();
            break;
        }    
        case SYS_EXIT: 
        {
            check_user_vaddr(f->esp + 4);
            sys_exit(ARG_INT(1));
            break;
        }
        case SYS_EXEC:
        {
            check_user_vaddr(f->esp + 4);
            void* cmd_line = ARG_PTR(1);
            
            check_user_string(cmd_line);
            f->eax = sys_exec(cmd_line);
            break;
        }
        case SYS_WAIT: 
        {
            check_user_vaddr(f->esp + 4);
            f->eax = sys_wait(ARG_INT(1));
            break;
        }
        case SYS_READ:
        {
            check_user_vaddr(f->esp + 4);
            check_user_vaddr(f->esp + 8);
            check_user_vaddr(f->esp + 12);
            void* buffer = ARG_PTR(2);
            check_user_vaddr(buffer);
            f->eax = sys_read(ARG_INT(1), buffer, ARG_INT(3));
            break;
        }
        case SYS_WRITE:
        {
            check_user_vaddr(f->esp + 4);
            check_user_vaddr(f->esp + 8);
            check_user_vaddr(f->esp + 12);
            int fd = ARG_INT(1);
            const void* buffer = ARG_PTR(2);
            unsigned size = ARG_INT(3);
            check_user_buffer(buffer,size);
            f->eax = sys_write(fd, buffer,size);
            break;
        }
        case SYS_FIBONACCI: 
        {
            check_user_vaddr(f->esp + 4); 
            f->eax = sys_fibonacci(ARG_INT(1));
            break;
        }
        case SYS_MAX_OF_FOUR_INT: 
        {
            check_user_vaddr(f->esp + 4); 
            check_user_vaddr(f->esp + 8); 
            check_user_vaddr(f->esp + 12); 
            check_user_vaddr(f->esp + 16); 
            f->eax = sys_max_of_four_int(ARG_INT(1), ARG_INT(2), ARG_INT(3), ARG_INT(4));
            break;
        }
        default:
        {
            sys_exit(-1);
            break;
        }
    }
}