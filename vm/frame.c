#include "frame.h"
#include "../threads/palloc.h"
#include "../lib/kernel/list.h"
#include "../threads/thread.h"

/* Frame implementation for VM list or simple array?
 */

struct list frame_table;

void
frame_table_init(){
	/* Initializes frame stuff */
	list_init(&frame_table);
	void *frame_ptr;
	struct *frame_table_entry; 

	while((frame_ptr = palloc_get_page(PAL_USER)) != NULL){
		frame_table_entry = malloc(sizeof(frame_table_entry));
		frame_table_entry->esp = frame_ptr; 
		frame_table_entry->owner_thread = thread_current();
		frame_table_entry->page = NULL;

		list_push_back(&frame_table, frame_table_entry->elem);
	}
}

/* Goes through the frame table to find a frame that's available.
 */
void *
frame_get(){
	
}
/* Frees the frame corresponding to the pointer.
 */
void 
frame_free(void *frame){
	
}

/* Evicts a frame and makes it available.
 */
void *
frame_evict(){
	
}
