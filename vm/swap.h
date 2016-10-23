#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <bitmap.h>
#include "devices/block.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "vm/frame.h"

/*
 * Swap implementation for VM
 */

#define SECTORS_IN_PAGE (PGSIZE/BLOCK_SECTOR_SIZE)

struct lock swap_lock;
struct bitmap *swap_table;
struct block *swap_block_device;

void swap_table_init(void);

int swap_to_disk(struct frame_table_entry *fte);
bool swap_from_disk(struct frame_table_entry *dest_fte, int swap_idx);

#endif
