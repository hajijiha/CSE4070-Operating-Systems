#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include <stdbool.h>
#include <inttypes.h>
#include "userprog/process.h"
#include "threads/interrupt.h" 
#include "userprog/pagedir.h"
#include "lib/syscall-nr.h"
void syscall_init (void);
void sys_halt (void);
void sys_exit (int status);
int sys_exec (const char *cmd_line);
int sys_wait (int pid);
int sys_read (int fd, void *buffer, unsigned size);
int sys_write (int fd, const void *buffer, unsigned size);

int sys_fibonacci(int n);
int sys_max_of_four_int(int a, int b, int c, int d);
#endif /* userprog/syscall.h */
