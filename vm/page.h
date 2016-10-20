#ifndef VM_PAGE_H
#define VM_PAGE_H

/* Page implementation for VM
 */

struct page_table_entry{
	void *user_va;

	bool is_valid;
};

void page_table_init();

#endif
