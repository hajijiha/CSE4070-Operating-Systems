#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stdbool.h>
#include "devices/block.h"

void swap_system_init (void);
void swap_in_page (size_t used_index, void *kpage);
size_t swap_out_page (void *kpage);
void swap_free_slot (size_t used_index);

#endif
