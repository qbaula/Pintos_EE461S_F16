/* Swap Implementation for Vm
 */

#include <bitmap.h>
#include <stdio.h>
#include "devices/block.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "userprog/syscall.h"
#include "vm/swap.h"
#include "vm/frame.h"

void
swap_table_init()
{
  swap_block_device = block_get_role(BLOCK_SWAP);
  if (swap_block_device == NULL)
    {
      printf("Swap could not be initialized. Possibly swap disk not created/found.\n");
      exit (-1);
    }

  swap_table = bitmap_create(block_size(swap_block_device) / SECTORS_IN_PAGE );
  bitmap_set_all(swap_table, false);

  lock_init (&swap_lock);
}

int
swap_to_disk (struct frame_table_entry *fte)
{
  // printf("Swapping to\n");
  // printf("Acquiring lock from thread %d\n", fte->owner->tid);
  lock_acquire (&swap_lock);
  // printf("Got lock thread %d\n", fte->owner->tid);
  int free_idx = bitmap_scan_and_flip (swap_table, 0, 1, false);
  if (free_idx == BITMAP_ERROR)
    {
      lock_release (&swap_lock);
      // printf("Released lock thread %d\n", fte->owner->tid);
      return -1;
    }
  
  int sector;
  char *src = (char *)(fte->frame_addr);
  for (sector = free_idx * SECTORS_IN_PAGE; sector < (free_idx + 1) * SECTORS_IN_PAGE;
       sector++)
    {
      block_write(swap_block_device, sector, src);
      src += BLOCK_SECTOR_SIZE;
		}

	memset(fte->frame_addr, 0, PGSIZE);

  lock_release (&swap_lock);
  // printf("Released lock thread %d\n", fte->owner->tid);
  return free_idx;
}

bool
swap_from_disk (struct frame_table_entry *dest_fte, int swap_idx)
{
  //printf("Swapping from %d, Thread owner: %d, Curr Thread%d\n", swap_idx);
  //print_all_spte();
  if (dest_fte == NULL)
    {
      PANIC("dest_fte in swap_from_disk is NULL\n");
    }

  // print_all_spte();
#if debugswapfrom
  printf ("address of swap_lock: %p\n", &swap_lock);
  printf ("address of swap_table: %p\n", &swap_table);
  printf ("address of swap_block_device: %p\n", &swap_block_device);

  printf ("swap_table points to: %p\n", swap_table);
  printf ("swap_block_device poitns to: %p\n", swap_block_device);
#endif

  lock_acquire (&swap_lock);
  bool is_resident = bitmap_test (swap_table, swap_idx);
  if (!is_resident)
    {
      lock_release (&swap_lock);
      PANIC("Frame not found in swap disk.\n");
    }

  int sector;
  char *dest = (char *)(dest_fte->frame_addr);
  for (sector = swap_idx * SECTORS_IN_PAGE; sector < (swap_idx + 1) * SECTORS_IN_PAGE;
       sector++)
    {
      block_read(swap_block_device, sector, (const void *) dest);
      dest += BLOCK_SECTOR_SIZE;
    }

  bitmap_set(swap_table, swap_idx, false);
  lock_release (&swap_lock);
  return true;
}

void
swap_clear(int clear_idx)
{
  lock_acquire (&swap_lock);
  bitmap_set(swap_table, clear_idx, false);
  lock_release (&swap_lock);
}
