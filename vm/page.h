#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stdbool.h>
#include <inttypes.h>
#include "kernel/list.h"
#include "filesys/off_t.h"
#include "filesys/file.h"
#include "threads/thread.h"

#define HEAP_STACK_DIVIDE 0xB0000000
#define CODE_START 0x8048000

/*
 * Page implementation for VM, outside programs will only have access to this
 * head file. They should NOT access frame.h unless running init. Though
 * that may be better off done here.
 */


struct sup_pte
{
  uint8_t *user_vaddr;

  /* determines state of the pte */
  bool valid;
  bool writable;
  bool accessed;
  bool dirty;
  
  /* true = pte in swap, else pte in frame table */
  bool in_swap;
  int swap_table_index;
  
  /* file information */
  bool is_file;
  bool is_stack;
  struct file *file;
  off_t offset;
  int read_bytes;
  int zero_bytes;
  bool has_been_loaded;

  struct list_elem elem;
};


/* Core functions */
void vm_page_table_init(struct list *spt);
struct sup_pte * get_spte(uint8_t *fault_addr);
void spt_clear(struct thread *owner);

/* Allocating new entries in the supplemental page table */
bool alloc_code_spte(struct file *file, off_t ofs, uint8_t *upage,
                     uint32_t read_bytes, uint32_t zero_bytes, bool writable);
bool alloc_blank_spte(uint8_t *upage);
bool load_spte (struct sup_pte *spte);

/* Debugging functions */
void print_all_spte(void);
void print_spte(struct sup_pte *pte);

#endif
