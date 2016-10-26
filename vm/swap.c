#include <bitmap.h>
#include <stdio.h>
#include <string.h>
#include "devices/block.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "userprog/syscall.h"
#include "vm/frame.h"
#include "vm/swap.h"

/*
 * Initializes the swap table by acquring the swap block device.
 * Creates the bitmap used to track free and used sectors.
 */
void
swap_table_init(void)
{
  swap_block_device = block_get_role(BLOCK_SWAP);
  if (swap_block_device == NULL)
    {
      printf("Swap could not be initialized. Possibly swap disk not created/found.\n");
      exit (-1);
    }

  /* 
   * Each bit in the bitmap represents a contiguous chunk of sectors
   * that can fit an entire frame or page.
   */
  swap_table = bitmap_create(block_size(swap_block_device) / SECTORS_IN_PAGE );
  bitmap_set_all(swap_table, false);

  lock_init (&swap_lock);
}

/*
 * Sets the sectors represented by clear_idx to be unused.
 */
void
swap_clear(int clear_idx)
{
  lock_acquire (&swap_lock);
  bitmap_set(swap_table, clear_idx, false);
  lock_release (&swap_lock);
}

/*
 * Writes an entire frame into the swap disk one sector (512 bytes) at a time.
 */
int
swap_to_disk (struct frame_table_entry *fte)
{
  lock_acquire (&swap_lock);

  /* Find the first free section of the swap disk that can fit a frame */
  uint32_t free_idx = bitmap_scan_and_flip (swap_table, 0, 1, false);
  if (free_idx == BITMAP_ERROR)
    {
      lock_release (&swap_lock);
      return -1;
    }
  
  /* Write the frame into the swap disk, one sector at a time */
  uint32_t sector;
  char *src = (char *)(fte->frame_addr);
  for (sector = free_idx * SECTORS_IN_PAGE; sector < (free_idx + 1) * SECTORS_IN_PAGE;
       sector++)
    {
      block_write(swap_block_device, sector, src);
      src += BLOCK_SECTOR_SIZE;
		}

  /* Clears the evicted frame in memory */
	memset(fte->frame_addr, 0, PGSIZE);
  lock_release (&swap_lock);

  return free_idx;
}

/*
 * Reads an entire frame from the swaps disk into a physical frame.
 * The swap_idx value represents which sectors to read from.
 */
bool
swap_from_disk (struct frame_table_entry *dest_fte, int swap_idx)
{
  lock_acquire (&swap_lock);
  if (dest_fte == NULL)
    {
      PANIC("dest_fte in swap_from_disk is NULL\n");
    }

  /* The requested sectors in the swap disk has to be used */
  bool is_resident = bitmap_test (swap_table, swap_idx);
  if (!is_resident)
    {
      PANIC("Frame not found in swap disk.\n");
    }

  /* Read the sectors from the swap disk one at a time */
  int sector;
  char *dest = (char *)(dest_fte->frame_addr);
  for (sector = swap_idx * SECTORS_IN_PAGE; sector < (swap_idx + 1) * SECTORS_IN_PAGE;
       sector++)
    {
      block_read(swap_block_device, sector, (void *) dest);
      dest += BLOCK_SECTOR_SIZE;
    }

  /* Indicate in the swap table that the sectors just read are now unused */
  bitmap_set(swap_table, swap_idx, false);
  lock_release (&swap_lock);

  return true;
}

