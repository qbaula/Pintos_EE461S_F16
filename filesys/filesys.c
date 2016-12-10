#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/thread.h"
#include "threads/malloc.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

struct dir *file_directory (const char *);
char *file_basename (const char *);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size, bool is_dir) 
{
  if (strcmp(name, "") == 0)
    {
      return false;
    }
  block_sector_t inode_sector = 0;
  struct dir *dir = file_directory(name);
  char *basename = file_basename(name);
  bool success = false;
  if(strcmp(basename, ".") && strcmp(basename, ".."))
    {
      success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, is_dir)
                  && dir_add (dir, basename, inode_sector));
    }
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);
  
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  if (strcmp(name, "") == 0)
    {
      return NULL;
    }
  struct dir *dir = file_directory(name);
  char *basename = file_basename(name);
  struct inode *inode = NULL;

  if (dir != NULL)
    dir_lookup (dir, basename, &inode);
  dir_close (dir);

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  struct dir *dir = file_directory(name);
  //struct dir *dir = dir_open_root();
  char *basename = file_basename(name);
  bool success = dir != NULL && dir_remove (dir, basename);
  dir_close (dir); 
  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 128))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}

bool
filesys_chdir (const char *name)
{
  char *path = (char *) malloc (strlen(name) + 3);
  memcpy (path, name, strlen(name) + 1);
  uint32_t mod_idx = strlen(name);
  path[mod_idx] = '/';
  path[mod_idx + 1] = '.';
  path[mod_idx + 2] = 0;
  struct dir* dir = file_directory(path);
  if (dir)
    {
      dir_close(thread_current()->cwd);
      thread_current()->cwd = dir;
      return true;
    }
  return false;
}

struct dir*
file_directory (const char *file_name)
{
  char *fn_copy;
  char *save_ptr, *next_token, *token;
  struct dir *dir;

  fn_copy = (char *) malloc(strlen(file_name) + 1);
  memcpy(fn_copy, file_name, strlen(file_name) + 1); 

  token = strtok_r(fn_copy, "/", &save_ptr);
  next_token = NULL;

  if(fn_copy[0] == '/' || !thread_current()->cwd)
    {
      dir = dir_open_root();
    }
  else 
    {
      dir = dir_reopen(thread_current()->cwd);
    }

  if(token)
    {
      next_token = strtok_r(NULL, "/", &save_ptr);
    }
  while(next_token != NULL)
    {
      if(strcmp(token, "."))
        {
          struct inode *inode;
          if(!dir_lookup_inode(dir, token, &inode))
            {
              return NULL;   
            }
          if (inode_is_dir(inode))
            {
              dir_close(dir);
              dir = dir_open(inode);
            }
          else
            {
              inode_close(inode);
            }            
        }

      token = next_token;
      next_token = strtok_r(NULL, "/", &save_ptr);
    }

  free(fn_copy);
  return dir;
}

char *
file_basename (const char *file_name)
{
  char *fn_copy;
  char *token, *save_ptr, *prev_token;
 
  if (strcmp(file_name, "/") == 0)
    {
      fn_copy = (char *) malloc(2);
      fn_copy[0] = '.';
      fn_copy[1] = 0;
      return fn_copy;
    }
  fn_copy = (char *) malloc(strlen(file_name) + 1);
  memcpy(fn_copy, file_name, strlen(file_name) + 1);
  prev_token = NULL;
  
  for (token = strtok_r(fn_copy, "/", &save_ptr); token != NULL;
    token = strtok_r (NULL, "/", &save_ptr))
    {
      prev_token = token;
    }
  char *basename = (char *) malloc(strlen(prev_token) + 1);
  memcpy(basename, prev_token, strlen(prev_token) + 1);
  
  free(fn_copy);
  return basename;
}


