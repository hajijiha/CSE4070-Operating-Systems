#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/synch.h"

typedef int mapid_t;

void syscall_init (void);
struct lock* get_fs_lock (void);
void sys_exit (int status);
void sys_munmap (mapid_t mapping);

#endif
