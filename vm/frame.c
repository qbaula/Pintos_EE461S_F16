#include "frame.h"
#include "../threads/palloc.h"

/* Frame implementation for VM list or simple array?
 */

void
frame_table_init(){
	/* Initializes frame stuff */
	
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
