#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "page.h"
/* Frame implementation for vm
 */

struct frame_table_entry {
	struct thread *owner_thread;
	struct sup_pte *page;
	void *esp;
};


void frame_table_init(void);

void *frame_get(); // allocates owner threads and page to determine its taken
void frame_free(void *frame); // deallocates ^^
void *frame_evict(); // calls swap to disk function and clears curr

#endif
