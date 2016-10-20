#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "page.h"
/* Frame implementation for vm
 */

struct frame_table_entry {
	struct thread *owner_thread;
	struct page_table_entry *page;
	void *esp;
};


void frame_table_init(void);

#endif
