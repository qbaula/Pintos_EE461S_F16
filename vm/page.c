#include "page.h"
#include "frame.h"

/* Page implementation for vim.
 */

void free_page (struct sup_pte *ptr);

void 
vm_page_table_init(){

}

/* Allocates a page in the page table by first checking to see if room avail
 * in the frame. If so, then get the frame # and allocate PTE. Otherwise, 
 * will need to evict a frame and make one available.
 */
void *
vm_get_page(){
	return frame_get();
}

/* This function must find the corresponding PTE of the the pointer passed in
 * and free the page.
 */
void 
vm_free_page(void *ptr){
	frame_free(ptr);
}

/* Function to talk with the frame based on the sup_pte
 */
void
free_page(struct sup_pte *ptr){
	
}

