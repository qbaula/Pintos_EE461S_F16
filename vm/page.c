#include <inttypes.h>
#include <list.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/off_t.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"

void spte_insert (struct list *sup_pt, struct sup_pte *pte);
bool in_same_page(uint8_t *vaddr1, uint8_t *vaddr2);

/*
 * Initializes the supplemental page table (SPT).
 */
void 
vm_page_table_init(struct list *spt)
{
  list_init(spt);  
}

/*
 * Returns a pointer to the SPT entry of the page
 * that contains fault_addr.
 */
struct sup_pte * 
get_spte(uint8_t *fault_addr)
{
  struct list_elem *e;
  for (e = list_begin(&thread_current()->spt); e != list_end(&thread_current()->spt);
       e = list_next(e))
    {
      struct sup_pte *spte = list_entry(e, struct sup_pte, elem);
      if (in_same_page(spte->user_vaddr, fault_addr))
        {
          return spte;
        }
    }

  return NULL;
}

/*
 * Returns true if the two virtual addresses are in the same page.
 */
bool 
in_same_page(uint8_t *vaddr1, uint8_t *vaddr2)
{
  return (pg_round_down(vaddr1) == pg_round_down(vaddr2));
}

/*
 * Clear SPT by uninstalling valid pages and freeing all swap table entries.
 * Free SPTEs as we go through the SPT list.
 * Free all frames that are owned by current thread.
 */
void
spt_clear(struct thread *owner)
{
  if (list_empty (&owner->spt) )
      {
        return;
      }

  struct list_elem *e, *prev = NULL;
  for (e = list_begin(&owner->spt); e != list_end(&owner->spt);
       e = list_next(e))
    {
      if (prev != NULL)
        {
          list_remove(prev);
          struct sup_pte *prev_spte = list_entry(prev, struct sup_pte, elem);
          free (prev_spte);
        }

      struct sup_pte *spte = list_entry(e, struct sup_pte, elem);
      if (pagedir_get_page (owner->pagedir, spte->user_vaddr))
        {
					pagedir_clear_page(owner->pagedir, spte->user_vaddr);
        }
      else if (spte->in_swap)
        {
          swap_clear(spte->swap_table_index);
        }

      prev = e;
    }
  if (prev != NULL)
    {
      list_remove(prev);
      struct sup_pte *prev_spte = list_entry(prev, struct sup_pte, elem);
      free (prev_spte);
    }

  frame_table_clear(owner);
}

void
spte_insert (struct list *sup_pt, struct sup_pte *pte)
{
  list_push_back(sup_pt, &pte->elem);  
}

/*
 * Allocates an SPTE for code segments or files.
 * Inputs:
 *  file  - file pointer to read from
 *  ofs   - offset of where to start reading from file
 *  upage - the user virtual address of the beginning of the page to map to
 *  read_bytes - how many bytes to read from file starting at ofs
 *  zero_bytes - how many bytes are expected to be zero at the end of the page
 *  writable - write permissions
 * Output:
 *  true if SPTE allocation was successful.
 *
 * Note that this function does not map a physical frame to the SPTE yet.
 * That mapping is handled in the page fault handler by calling load_spte()
 */
bool 
alloc_code_spte(struct file *file, off_t ofs, uint8_t *upage,
                uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
#ifdef debug4
  printf("read_bytes: %d\n", read_bytes);
  printf("zero_btyes: %d\n", zero_bytes);
#endif
  ASSERT (read_bytes + zero_bytes == PGSIZE);

  struct sup_pte *new_spte = (struct sup_pte *) malloc (sizeof(struct sup_pte));
  if (new_spte == NULL)
    {
      return false;
    }

  new_spte->valid = false;
  new_spte->accessed = false;
  new_spte->dirty = false;

  new_spte->in_swap = false;

  new_spte->is_stack = false;
  
  new_spte->is_file = true;
  new_spte->file = file;
  new_spte->offset = ofs;
  new_spte->user_vaddr = upage;
  new_spte->read_bytes = read_bytes;
  new_spte->zero_bytes = zero_bytes;
  new_spte->writable = writable;
  new_spte->has_been_loaded = false;

  spte_insert(&thread_current()->spt, new_spte); 

#if debug3
  print_spte(new_spte);
#endif
  return true;
}

/*
 * Allocates an SPTE for stack pages. A physical frame will be mapped 
 * to the SPTE and the page will be zeroed out.
 */
bool
alloc_blank_spte(uint8_t *upage)
{
  struct sup_pte *new_spte = (struct sup_pte *) malloc (sizeof(struct sup_pte));
  if (new_spte == NULL)
    {
      return false;
    }

  new_spte->user_vaddr = pg_round_down(upage);

  new_spte->writable = true;
  new_spte->accessed = false;
  new_spte->dirty = false;

  new_spte->in_swap = false;
  new_spte->is_stack = true;

  new_spte->is_file = false;
  new_spte->file = NULL;
  new_spte->offset = 0;
  new_spte->read_bytes = 0;
  new_spte->zero_bytes = 0;

  spte_insert(&thread_current()->spt, new_spte); 

  struct frame_table_entry *fte = frame_map (new_spte);
  if (!fte)
    {
      PANIC ("Can't map to page table\n");
    }

  memset(fte->frame_addr, 0, PGSIZE);
  new_spte->valid = true;
  fte->in_edit = false;

  return true;
}

/*
 * Loads an SPTE by mapping it to a physical frame.
 * Determines whether it should load from swap
 * or read from a file.
 */
bool 
load_spte (struct sup_pte *spte)
{
  struct frame_table_entry *fte = frame_map(spte);
  if (fte == NULL)
    {
      return false;
    }

	if (spte->in_swap)
		{
			swap_from_disk (fte, spte->swap_table_index);
			spte->in_swap = false;
		}
  else if (spte->is_file && !spte->has_been_loaded)
    {
      int actual_read = 0;
      lock_acquire(&file_lock);
      actual_read = file_read_at (spte->file, fte->frame_addr, 
                                  spte->read_bytes, spte->offset);
      lock_release(&file_lock);

      if (actual_read != spte->read_bytes)
        {
          PANIC ("File or code could not be read properly.\n");
        }

      memset(fte->frame_addr + spte->read_bytes, 0, spte->zero_bytes);
      spte->has_been_loaded = true;
    }

  if (fte)
    {
      fte->in_edit = false;
    }
  spte->valid = true;
  return true;
}

void
print_spte(struct sup_pte *pte)
{
  printf("addr: %p\n", pte);
  printf("user_vaddr: %p\n", pte->user_vaddr);
  printf("valid: %d\n", pte->valid);
  printf("writable: %d\n", pte->writable);
  printf("accessed: %d\n", pte->accessed);
  printf("dirty: %d\n", pte->dirty);
  printf("stack: %d\n", pte->is_stack);
  printf("in_swap: %d\n", pte->in_swap);
  printf("swap_table index: %d\n", pte->swap_table_index);
  printf("is_file: %d\n", pte->is_file);
  printf("file: %p\n", pte->file);
  printf("offset: %d\n", (int)pte->offset);
  printf("read_bytes: %d\n", pte->read_bytes);
  printf("zero_bytes: %d\n", pte->zero_bytes);
  printf("\n");
}

void
print_all_spte()
{
  printf("Printing all SPTEs\n");
  int i = 0;
  struct list_elem *e;
  for (e = list_begin(&thread_current()->spt); e != list_end(&thread_current()->spt);
       e = list_next(e))
    {
      printf("SPTE: %d\n", i);
      struct sup_pte *spte = list_entry(e, struct sup_pte, elem);
      print_spte(spte);
      i++;
    }
}
