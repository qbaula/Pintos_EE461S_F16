#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "page.h"
#include "../lib/kernel/list.h"

/*
 * Frame implementation for vm
 */
struct frame_table_entry {
	struct thread *owner;
	struct sup_pte *spte;
	void *frame_addr;
};

void frame_table_init(void);

struct frame_table_entry *frame_get();  // get a free page that is resident in memory
struct frame_table_entry *frame_map(struct sup_pte *spte);
void frame_table_clear(struct thread *owner);
void frame_table_destroy();
void frame_free(struct frame_table_entry *frame);           // deallocates a frame (i.e. no owner)
struct frame_table_entry *frame_evict();                    // calls swap to disk function and clears curr

void frame_print (struct frame_table_entry *fte, int num_bytes);
#endif
