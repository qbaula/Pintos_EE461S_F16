#include "frame.h"
#include "lib/kernel/list.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"
#include "vm/page.h"


/* 
 * Frame implementation for VM list or simple array?
 */
struct frame_table_entry *frame_table;

void
frame_table_init ()
{
  int user_pages = palloc_get_num_user_pages ();
  frame_table = (struct frame_table_entry *)(malloc(sizeof(struct frame_table_entry) * user_pages));
  printf("size of frame_table: %d\n", sizeof(struct frame_table_entry) * user_pages);

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
          frame_table[i].owner = thread_current();
          return &frame_table[i];
        }
    }
  // panic if absolutely full memory
}

struct frame_table_entry *
frame_map(struct sup_pte *spte)
{
  struct thread *t = thread_current();
  struct frame_table_entry *fte = frame_get();
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
      fte->owner = NULL;
      return NULL;
    }
}

/*
 * Frees the frame corresponding to the pointer.
 */
void 
frame_free(struct frame_table_entry *frame)
{
  pagedir_clear_page(frame->owner->pagedir, frame->spte->user_va);
  frame->owner = NULL;	

  frame->spte->valid = false;
  frame->spte = NULL;
}

/*
 * Evicts a frame and makes it available.
 */
void *
frame_evict()
{
	
}
