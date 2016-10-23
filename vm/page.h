#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stdbool.h>
#include <inttypes.h>
#include "kernel/list.h"
#include "filesys/off_t.h"
#include "filesys/file.h"

#define HEAP_STACK_DIVIDE 0xB0000000
#define CODE_START 0x8048000

/* Page implementation for VM, outside programs will only have access to this
 * head file. They should NOT access frame.h unless running init. Though
 * that may be better off done here.
 */

struct sup_pte
{
  uint8_t *user_va;

  /* determines state of the pte */
  bool valid;
  bool writable;
  bool accessed;
  bool dirty;
  
  /* true = pte in swap, else pte in frame table */
  bool in_swap;
  int swap_table_index;
  
  /* files */
  bool is_file;
  struct file *file;
  off_t offset;
  int read_bytes;
  int zero_bytes;

  struct list_elem elem;
};

void vm_page_table_init();
void *vm_get_page();
void vm_free_page(void *);

bool alloc_code_spte(struct file *file, off_t ofs, uint8_t *upage,
                     uint32_t read_bytes, uint32_t zero_bytes, bool writable);
bool alloc_blank_spte(uint8_t *upage);
struct sup_pte * get_spte(uint8_t *fault_addr);

void print_all_spte();
void print_spte(struct sup_pte *pte);
#endif
