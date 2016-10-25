#include <stdio.h>
#include "frame.h"
#include "lib/kernel/list.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"
#include "vm/page.h"
#include "vm/swap.h"

/* 
 * Frame implementation for VM list or simple array?
 */
struct frame_table_entry *frame_table;
struct lock frame_lock;

void
frame_table_init ()
{
  int user_pages = palloc_get_num_user_pages ();
  frame_table = (struct frame_table_entry *)(malloc(sizeof(struct frame_table_entry) * user_pages));

  void *frame_ptr;
  struct frame_table_entry *fte_ptr; 
  int i = 0;
  for (i = 0; i < palloc_get_num_user_pages(); i++)
    {
      frame_ptr = palloc_get_page(PAL_USER);
      frame_table[i].frame_addr = frame_ptr; 
      frame_table[i].owner_tid = -1;
      frame_table[i].spte = NULL;
    }

  lock_init (&frame_lock);
}

/*
 * Goes through the frame table to find a frame that's available.
 * Does that by checking if owner == -1. If all frames are owned,
 * then call frame_evict to get a free frame.
 */
struct frame_table_entry *
frame_get()
{
  int i;
  for (i = 0; i < palloc_get_num_user_pages(); i++)
    {
      if (frame_table[i].owner_tid == -1)
        {
					frame_table[i].spte = NULL;
          frame_table[i].owner_tid = thread_current()->tid;
          return &frame_table[i];
        }
    }
  
  struct frame_table_entry *efte = frame_evict();
	efte->spte = NULL;
  return efte;
}

/*
 * Calls frame_get to get a free frame and maps that frame to 
 * the given supplemental page table entry.
 */
struct frame_table_entry *
frame_map(struct sup_pte *spte)
{
  struct thread *t = thread_current();

	lock_acquire (&frame_lock);
  struct frame_table_entry *fte = frame_get();
  lock_release(&frame_lock);

  fte->spte = spte;


  bool success = (pagedir_get_page (t->pagedir, spte->user_va) == NULL
          && pagedir_set_page (t->pagedir, spte->user_va, fte->frame_addr, spte->writable));

  if (success)
    {
      spte->valid = true;
      return fte;
    }
  else
    {
      // deallocate frame
			spte->valid = false;
      fte->owner_tid = -1;
			fte->spte = NULL;
      return NULL;
    }
}

/*
 * Marks the frames owned by owner to be unowned and free.
 */
void 
frame_table_clear(struct thread *owner)
{
  
  lock_acquire (&frame_lock);
  int i;
  for (i = 0; i < palloc_get_num_user_pages(); i++)
    {
      if (frame_table[i].owner_tid == owner->tid)
        {
          frame_table[i].owner_tid = -1;
					frame_table[i].spte = NULL;
        }
    }
  lock_release(&frame_lock);
}

/*
 * When pintos main() exits, all the pages in the user pool are freed.
 */
void
frame_table_destroy()
{
  //lock_release(&frame_lock);
  int i;
  for (i = 0; i < palloc_get_num_user_pages(); i++)
    {
      palloc_free_page (frame_table[i].frame_addr);
    }
  //lock_release(&frame_lock);
}

/*
 * Removes page table mapping for current frame and then
 * sends the frame to swap disk.
 */
struct frame_table_entry *
frame_swap(struct frame_table_entry *fte)
{
  struct sup_pte *evicted_spte = fte->spte;
	struct thread *evicted_thread = thread_get(fte->owner_tid);
	if (evicted_thread)
	  {
      pagedir_clear_page(evicted_thread->pagedir, evicted_spte->user_va);
		}

  evicted_spte->in_swap = true; 
  evicted_spte->swap_table_index = swap_to_disk(fte);
  if (evicted_spte->swap_table_index == -1)
    {
      PANIC ("Swap full\n");
    }

  evicted_spte->valid = false;
  fte->owner_tid = thread_current()->tid;

	return fte;
}

/*
 * Evicts a frame and makes it available.
 * Finds the first frame that is not owned by the current thread.
 * If all frames are owned by current thread, pick any frame
 * that isn't the stack.
 * All else, pick the 51st frame. 
 *
 * Once a frame is chosen to be evicted, call frame_swap() to send that
 * frame to the swap disk (The frame is zeroed out by swap_to_disk()).
 */
struct frame_table_entry *
frame_evict()
{
	tid_t current_tid = thread_current()->tid;

  // Iterate over frames looking for one that is not owned by current thread
  int i;
  for (i = 0; i < palloc_get_num_user_pages(); i++)
    {
      if (frame_table[i].owner_tid != current_tid && !frame_table[i].spte->is_stack)
        {
          return frame_swap(&frame_table[i]);
        }
    }

  // If we got here, every frame is owned by the current thread
  for(i = palloc_get_num_user_pages() -1; i > 0; --i)
    {
      if(!frame_table[i].spte->is_stack)
        {
          return frame_swap(&frame_table[i]);
        }
    }

  // SHOULD NEVER BE CALLED
  printf("Everything on the frame is a stck\n");
  return frame_swap(&frame_table[50]);
}
  
void 
frame_print (struct frame_table_entry *fte, int num_bytes)
{
  printf("\n******************************\n");
  printf("Printing a frame for %d bytes\n", num_bytes);
  int i;
  char *f = (char *)(fte->frame_addr);
  for (i = 1; i < num_bytes+1; i++)
    {
      printf("%02X ", *f);
      f++;
      if (i % 64 == 0)
        {
          printf("\n");
        }
    }
  printf("\n******************************\n");
}

