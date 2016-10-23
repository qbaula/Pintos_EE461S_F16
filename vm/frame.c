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
  // printf("size of frame_table: %d\n", sizeof(struct frame_table_entry) * user_pages);

  void *frame_ptr;
  struct frame_table_entry *fte_ptr; 
  int i = 0;
  // while((frame_ptr = palloc_get_page(PAL_USER)) != NULL)
  for (i = 0; i < palloc_get_num_user_pages(); i++)
    {
      frame_ptr = palloc_get_page(PAL_USER);
      frame_table[i].frame_addr = frame_ptr; 
      frame_table[i].owner= NULL;
      frame_table[i].spte = NULL;
    }

  lock_init (&frame_lock);
}

/*
 * Goes through the frame table to find a frame that's available.
 */
struct frame_table_entry *
frame_get()
{
  int i;
  for (i = 0; i < palloc_get_num_user_pages(); i++)
    {
      if (frame_table[i].owner == NULL)
        {
          // printf("Gave frame to %d\n", thread_current()->tid);
          frame_table[i].owner = thread_current();
          return &frame_table[i];
        }
    }
  
  // no free frames
  return frame_evict();
}

struct frame_table_entry *
frame_map(struct sup_pte *spte)
{
  struct thread *t = thread_current();

  // printf("Acquiring lock for thread %d\n", t->tid);
  lock_acquire (&frame_lock);
  // printf("Acquired lock for thread %d\n", t->tid);
  struct frame_table_entry *fte = frame_get();
  lock_release(&frame_lock);
  // printf("Released lock for thread %d\n", t->tid);

  fte->spte = spte;

  if (spte->in_swap)
    {
      swap_from_disk (fte, spte->swap_table_index);
      spte->in_swap = false;
      // print_spte (spte);
      // printf ("Swapping frame from disk: %p\n", fte->frame_addr);
      // frame_print (fte, 4096);
    }

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
      fte->owner = NULL;
      return NULL;
    }
}

void 
frame_table_clear(struct thread *owner)
{
  lock_acquire (&frame_lock);
  int i;
  for (i = 0; i < palloc_get_num_user_pages(); i++)
    {
      if (frame_table[i].owner == owner)
        {
          frame_table[i].owner = NULL;
        }
    }
  lock_release(&frame_lock);
}

/*
 * Frees the frame corresponding to the pointer.
void 
frame_free(struct frame_table_entry *frame)
{
  pagedir_clear_page(frame->owner->pagedir, frame->spte->user_va);
  frame->owner = NULL;	

  frame->spte->valid = false;
  frame->spte = NULL;
}
 */

void
frame_swap(struct frame_table_entry *fte)
{
  struct sup_pte *owner_spte;
  owner_spte = fte->spte;
  pagedir_clear_page(fte->owner->pagedir, owner_spte->user_va);

  owner_spte->in_swap = true; 
  // printf ("Cleared virtual address: %p\n", owner_spte->user_va);
  owner_spte->swap_table_index = swap_to_disk(fte);
  if (owner_spte->swap_table_index == -1)
    {
      // no system memory left
      PANIC ("Swap full\n");
    }

  owner_spte->valid = false;
  fte->owner = thread_current();
}

/*
 * Evicts a frame and makes it available.
 */
struct frame_table_entry *
frame_evict()
{
  struct thread *owner = thread_current();

  // iterate over frames looking for one that is not owned by current thread
  int i;
  for (i = 0; i < palloc_get_num_user_pages(); i++)
    {
      if (frame_table[i].owner != owner)
        {
          // printf ("Evict frame #: %d", i);
          frame_swap(&frame_table[i]);
          return &frame_table[i];
        }
    }

  // if we got here, every frame is owned by the current thread
  // printf ("Evict frame #: %d\n", 50);
  frame_swap(&frame_table[50]);
  return &frame_table[50];
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

