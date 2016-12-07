#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include <stdio.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/synch.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
#define MAX_DIRECT_BLOCKS 12
#define MAX_FILE_SIZE 8460288
#define BLOCKS_PER_INDIRECT 128


typedef block_sector_t inode_sector;
typedef block_sector_t data_sector;


/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
    inode_sector parent;
    data_sector direct_blocks[MAX_DIRECT_BLOCKS]; 
    data_sector indirect_block;
    data_sector doubly_indirect_block;
    bool is_dir;
    unsigned magic;                     /* Magic number. */
    uint32_t unused[110];               /* Not used. */
  };

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    inode_sector sector;                /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct lock inode_lock;             /* Lock if needs to expand. */
    struct inode_disk data;             /* Inode content. */
  };

struct indirect_block
  {
    data_sector ptr[BLOCKS_PER_INDIRECT];
  };


bool inode_alloc (struct inode_disk *disk_inode);
void inode_dealloc (struct inode_disk *disk_inode);
void inode_dealloc_indirect (data_sector sector);
void inode_dealloc_doubly_indirect (data_sector sector);
bool inode_extend (struct inode_disk *disk_inode, off_t length);
bool inode_extend_indirect (data_sector *sector, size_t num_sectors);
bool inode_extend_doubly_indirect (data_sector *sector, size_t num_sectors);
bool allocate_sector (data_sector *sector);
void deallocate_sector (data_sector sector);

void print_single_indirect (block_sector_t sector);
void print_doubly_indirect (block_sector_t sector);

inline size_t
min (size_t a, size_t b)
{
  return a < b ? a : b;
}

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  if (pos >= 0 && pos < inode->data.length)
    {
      uint32_t block_idx = pos / BLOCK_SECTOR_SIZE;
      if (block_idx < MAX_DIRECT_BLOCKS)
        {
          return inode->data.direct_blocks[block_idx];
        }

      else if (block_idx < MAX_DIRECT_BLOCKS + BLOCKS_PER_INDIRECT)
        {
          struct indirect_block indirect_block;
          block_read(fs_device, 
                     (block_sector_t) inode->data.indirect_block, &indirect_block);
          return indirect_block.ptr[block_idx - MAX_DIRECT_BLOCKS];
        }

      else
        {
          struct indirect_block doubly_indirect_block, indirect_block;
          uint32_t doubly_block_idx, singly_block_idx;
          block_idx = block_idx - (MAX_DIRECT_BLOCKS + BLOCKS_PER_INDIRECT);
          doubly_block_idx = block_idx / BLOCKS_PER_INDIRECT;
          singly_block_idx = block_idx % BLOCKS_PER_INDIRECT;

          block_read(fs_device, 
                     (block_sector_t) inode->data.doubly_indirect_block,
                     &doubly_indirect_block);
          block_read(fs_device, 
                     (block_sector_t) doubly_indirect_block.ptr[doubly_block_idx],
                     &indirect_block);
          return indirect_block.ptr[singly_block_idx];
        }
    }
  else
    {
      return -1;
    }
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, bool is_dir)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  if (length > MAX_FILE_SIZE) 
    {
      return false;
    }

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      disk_inode->length = length;
      disk_inode->is_dir = is_dir;
      disk_inode->parent = ROOT_DIR_SECTOR;
      disk_inode->magic = INODE_MAGIC;
      if (inode_alloc (disk_inode))
        {
          block_write (fs_device, sector, disk_inode);
          success = true; 
        }
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init(&inode->inode_lock);
  block_read (fs_device, inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          free_map_release (inode->sector, 1);
          inode_dealloc (&inode->data);  
        }
      else 
        {
          block_write(fs_device, (block_sector_t) inode->sector, &inode->data); 
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          block_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else 
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          block_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  // if (byte_to_sector(inode, offset + size) == -1)
  if (offset + size >= inode->data.length)
    {
      lock_acquire(&inode->inode_lock);

      if (offset + size >= inode->data.length)
      {
        if (!inode_extend (&inode->data, offset + size)) 
          {
            return 0;
          }
        inode->data.length = offset + size;
      }

      lock_release(&inode->inode_lock);
    }

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      if (sector_idx == 0)
        {
          printf("offset: %d\n", offset);
          printf("offset+size: %d\n", offset + size);
          printf("inode length: %d\n", inode->data.length);
          print_doubly_indirect (inode->data.doubly_indirect_block);
        }
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          block_write (fs_device, sector_idx, buffer + bytes_written);
        }
      else 
        {
          /* We need a bounce buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left) 
            block_read (fs_device, sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          block_write (fs_device, sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

bool inode_alloc (struct inode_disk *disk_inode)
{
  return inode_extend (disk_inode, disk_inode->length);
}

bool inode_extend (struct inode_disk *disk_inode, off_t length) 
{
  size_t num_sectors = bytes_to_sectors (length);

  if(length > MAX_FILE_SIZE) 
    {
      return false;
    }

  /* Direct Blocks */
  uint32_t i;
  size_t alloc_sectors;
  alloc_sectors = min(num_sectors, MAX_DIRECT_BLOCKS);
  for (i = 0; i < alloc_sectors; i++) 
    {
      if (disk_inode->direct_blocks[i] == 0) 
        {
          if(!free_map_allocate(1, &disk_inode->direct_blocks[i]))
            {
              return false;
            }
        } 
    }

  num_sectors -= alloc_sectors;
  if(num_sectors == 0) 
    {
      return true;
    }

  /* Indirect Block */
  alloc_sectors = min(num_sectors, BLOCKS_PER_INDIRECT);
  if(!inode_extend_indirect(&disk_inode->indirect_block, alloc_sectors))
    {
      return false; 
    }
  
  num_sectors -= alloc_sectors;
  if(num_sectors == 0) 
    {
      return true;
    }

  /* Doubly Indirect Block */
  alloc_sectors = min(num_sectors, BLOCKS_PER_INDIRECT * BLOCKS_PER_INDIRECT);
  if(!inode_extend_doubly_indirect(&disk_inode->doubly_indirect_block, alloc_sectors))
    {
      return false; 
    }
  
  return true; 
}

bool allocate_sector (data_sector *sector)
{
  if(*sector == 0)
    {
      if(!free_map_allocate(1, (block_sector_t *) sector))
        {
          return false;
        }
    }
  return true;
}

bool inode_extend_indirect (data_sector *sector, size_t num_sectors)
{
  struct indirect_block indirect_block;
  if(!allocate_sector(sector))
    {
      return false;
    }  
  block_read(fs_device, (block_sector_t) *sector, &indirect_block);
  uint32_t i;
  for(i = 0; i < num_sectors; i++)
    { 
      if(!allocate_sector(&indirect_block.ptr[i]))
        {
          return false;
        }
    }
  
  block_write(fs_device, (block_sector_t) *sector, &indirect_block);
  return true;
}

bool inode_extend_doubly_indirect (data_sector *sector, size_t num_sectors)
{
  struct indirect_block doubly_indirect_block;
  if(!allocate_sector(sector))
    {
      return false;
    }
  block_read(fs_device, (block_sector_t) *sector, &doubly_indirect_block);
  uint32_t i;
  size_t alloc_sectors, num_indirect = DIV_ROUND_UP(num_sectors, BLOCKS_PER_INDIRECT); 
  for(i = 0; i < num_indirect; i++)
    {
      alloc_sectors = min(num_sectors, BLOCKS_PER_INDIRECT);
      if(!inode_extend_indirect(&doubly_indirect_block.ptr[i], alloc_sectors))
        {
          return false; 
        }
  
      num_sectors -= alloc_sectors;
      if(num_sectors == 0) 
        {
          break;
        }
    }
  block_write(fs_device, (block_sector_t) *sector, &doubly_indirect_block);
  return true;
}

void
deallocate_sector (data_sector sector) 
{
  if(sector)
    {
      free_map_release((block_sector_t) sector, 1);
    }
}

void
inode_dealloc (struct inode_disk *disk_inode) 
{
  /* Direct Blocks */  
  int i;
  for( i = 0; i < MAX_DIRECT_BLOCKS; i++ )
    {
      deallocate_sector(disk_inode->direct_blocks[i]);  
    }
  
  /* Indirect Block */
  inode_dealloc_indirect(disk_inode->indirect_block);

  /* Doubly Indirect Block */
  inode_dealloc_doubly_indirect(disk_inode->doubly_indirect_block);
}

void
inode_dealloc_indirect (data_sector sector) 
{
  if(sector == 0)
    {
      return;
    }
  struct indirect_block indirect_block;
  block_read(fs_device, (block_sector_t) sector, &indirect_block);
  int i;
  for(i = 0; i < BLOCKS_PER_INDIRECT; i++)
    {
      deallocate_sector(indirect_block.ptr[i]);
    }
  deallocate_sector(sector);
}

void
inode_dealloc_doubly_indirect(data_sector sector)
{
  if(sector == 0)
    {
      return;
    }
  struct indirect_block doubly_indirect_block;
  block_read(fs_device, (block_sector_t) sector, &doubly_indirect_block);
  int i;
  for(i = 0; i < BLOCKS_PER_INDIRECT; i++)
    {
      inode_dealloc_indirect(doubly_indirect_block.ptr[i]);
    }
  deallocate_sector(sector);
}

void
print_single_indirect (block_sector_t sector)
{
  struct indirect_block block;
  printf("Indirect sector: %d\n", sector);
  if (sector)
    {
      block_read(fs_device, sector, &block);
      int i;
      for (i = 0; i < BLOCKS_PER_INDIRECT; i++) 
        {
          printf ("%8d", block.ptr[i]);
          if (i % 8 == 0)
            {
              printf("\n");
            }
        }
    }
}

void
print_doubly_indirect (block_sector_t sector)
{
  struct indirect_block block;
  printf("Doubly indirect sector: %d\n", sector);
  if (sector)
    {
      block_read (fs_device, sector, &block);
      int i;
      for (i = 0; i < BLOCKS_PER_INDIRECT; i++)
        {
          print_single_indirect (block.ptr[i]);
          printf("\n");
        }
    }
}
