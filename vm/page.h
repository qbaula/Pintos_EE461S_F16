#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stdbool.h>

/* Page implementation for VM, outside programs will only have access to this
 * head file. They should NOT access frame.h unless running init. Though
 * that may be better off done here.
 */

struct sup_pte{
	/* determines state of the pte */
	bool valid;
	bool writable;
	bool accessed;
	bool dirty;
	
	/* true = pte in swap, else pte in frame table */
	bool in_swap;
};

void vm_page_table_init();

void *vm_get_page();

void vm_free_page(void *);

#endif
