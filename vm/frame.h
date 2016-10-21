#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "page.h"
#include "../lib/kernel/list.h"

/*
 * Frame implementation for vm
 */
struct frame_table_entry {
	struct thread *owner;
	struct sup_pte *page;
	void *frame_addr;
};

void frame_table_init(void);

void *frame_get();              // get a free page that is resident in memory
void frame_free(void *frame);   // deallocates a frame (i.e. no owner)
void *frame_evict();            // calls swap to disk function and clears curr

#endif
