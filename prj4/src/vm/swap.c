#include "vm/swap.h"
#include <bitmap.h>
#include "lib/kernel/bitmap.h"
#include "threads/vaddr.h"
#include "threads/synch.h"

static struct lock swap_lock;
static struct bitmap *swap_map;
static struct block *swap_block;

void
swap_system_init (void)
{
  swap_block = block_get_role (BLOCK_SWAP);
  if (swap_block == NULL)
    return;
  swap_map = bitmap_create (block_size (swap_block) / (PGSIZE / BLOCK_SECTOR_SIZE));
  if (swap_map == NULL)
    return;
  bitmap_set_all (swap_map, false);
  lock_init (&swap_lock);
}

void
swap_in_page (size_t used_index, void *kpage)
{
  if (swap_block == NULL || swap_map == NULL)
    return;

  lock_acquire (&swap_lock);
  if (bitmap_test (swap_map, used_index) == false)
    {
       lock_release (&swap_lock);
       return;
    }

  int i;
  for (i = 0; i < (PGSIZE / BLOCK_SECTOR_SIZE); i++)
    {
      block_read (swap_block, used_index * (PGSIZE / BLOCK_SECTOR_SIZE) + i, kpage + (i * BLOCK_SECTOR_SIZE));
    }
  bitmap_set (swap_map, used_index, false);
  lock_release (&swap_lock);
}

size_t
swap_out_page (void *kpage)
{
  if (swap_block == NULL || swap_map == NULL)
    PANIC ("Swap not initialized");

  lock_acquire (&swap_lock);
  size_t free_index = bitmap_scan_and_flip (swap_map, 0, 1, false);
  if (free_index == BITMAP_ERROR)
    PANIC ("Swap full");

  int i;
  for (i = 0; i < (PGSIZE / BLOCK_SECTOR_SIZE); i++)
    {
      block_write (swap_block, free_index * (PGSIZE / BLOCK_SECTOR_SIZE) + i, kpage + (i * BLOCK_SECTOR_SIZE));
    }
  lock_release (&swap_lock);

  return free_index;
}

void
swap_free_slot (size_t used_index)
{
   if (swap_block == NULL || swap_map == NULL)
     return;
     
   lock_acquire (&swap_lock);
   if (bitmap_test (swap_map, used_index) == true)
       bitmap_set (swap_map, used_index, false);
   lock_release (&swap_lock);
}
