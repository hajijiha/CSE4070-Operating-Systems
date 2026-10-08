#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include "filesys/off_t.h"

enum spt_type {
    SPT_BIN,
    SPT_FILE,
    SPT_ANON,
    SPT_SWAP
};

struct spt_entry {
    void *uva;
    void *kpage;
    enum spt_type type;
    bool writable;
    bool is_loaded;
    
    struct file *file;
    off_t offset;
    uint32_t read_bytes;
    uint32_t zero_bytes;
    
    size_t swap_slot;
    
    struct hash_elem elem;
};

void spt_init (struct hash *spt);
void spt_destroy (struct hash *spt);
struct spt_entry *spt_find (struct hash *spt, void *uva);
bool spt_insert (struct hash *spt, struct spt_entry *p);
bool spt_load (struct spt_entry *p);

#endif
