#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "lib/kernel/list.h"
#include "vm/page.h"
#include "threads/thread.h"
#include "threads/palloc.h"

struct phys_frame {
    void *kpage;
    struct spt_entry *spte;
    struct thread *thread;
    struct list_elem elem;
};

void frame_system_init (void);
void *frame_alloc (enum palloc_flags flags, struct spt_entry *spte);
void frame_free (void *kpage);
void frame_cleanup (struct thread *t);

#endif
