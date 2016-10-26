#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <list.h>
#include "threads/thread.h"
#include "vm/page.h"

/*
 * Frame implementation for vm
 */
struct frame_table_entry {
	tid_t owner_tid;
	struct sup_pte *spte;
	void *frame_addr;
	bool in_edit;
};

void frame_table_init(void);

struct frame_table_entry *frame_get(void);  
struct frame_table_entry *frame_map(struct sup_pte *spte);
void frame_table_clear(struct thread *owner);
void frame_table_destroy(void);

struct frame_table_entry * frame_swap(struct frame_table_entry *fte);
struct frame_table_entry *frame_evict(void);    

void frame_print (struct frame_table_entry *fte, int num_bytes);
#endif
