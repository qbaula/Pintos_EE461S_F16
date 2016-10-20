#ifndef VM_SWAP_H
#define VM_SWAP_H

#include "../devices/block.h"

/* Swap implementation for VM
 */

void swap_table_init(void);

bool swap_to_disk(void *frame);
bool swap_from_disk(void *frame);

#endif
