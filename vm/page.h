#ifndef VM_PAGE_H
#define VM_PAGE_H

/* Page implementation for VM, outside programs will only have access to this
 * head file. They should NOT access frame.h unless running init. Though
 * that may be better off done here.
 */

struct sup_pte{
	void *user_va;

	/* determines state of the pte */
	bool valid;
	bool accessed;
	bool dirty;
	bool writable;
	
	/* true = pte in swap, else pte in frame table */
	bool swap;
};

void vm_page_table_init();

void *vm_get_page();

void vm_free_page(void *);

#endif
