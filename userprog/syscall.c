#include <stdio.h>
#include <syscall-nr.h>
#include <stdbool.h>
#include <string.h>
#include "devices/shutdown.h"
#include "userprog/syscall.h"
#include "userprog/process.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

#include "devices/input.h"
#include "userprog/pagedir.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "filesys/filesys.h"
#include "filesys/file.h"

#include "vm/page.h"
#include "vm/frame.h"

#define PTR_READ 0
#define PTR_WRITE 1

static void syscall_handler (struct intr_frame *);
int get_arg (void *esp, uint32_t *args, int num_args);

/* File system private functions. */
struct file* fd_to_file(struct thread* t, int fd);
bool ptr_valid(void *esp, const void* ptr, int len, bool is_write);
bool is_open(struct thread* t, int fd);

/* Checks if a threads's given file descriptor is valid/open.
 * Assumes that this is for files and not STDIN/STDOUT. */
bool is_open(struct thread* t, int fd)
{
  bool isOpen;

  /* Cannot be STDIN/STDOUT. */
  if (fd == 0 || fd == 1)
    {
      isOpen = false;
    }

  else 
    {
      /* Range check. */
      if (fd >= t->open_files->size || fd < 0)
        {
          isOpen = false;
        }

      /* Range valid, check fd. */
      else 
        {
          isOpen = t->open_files->isOpen[fd];
        }
    }

 return isOpen; 
}

/* File system common function converting a file descriptor (fd) to a file pointer.
 * Returns a NULL if fd is STDIN, STDIN, or a not open file. */
struct file* fd_to_file(struct thread* t, int fd){
  struct file* f;    

  /* Check validity of fd. */
  if (!is_open(t, fd))
    {
      f = NULL;
    }
  else 
    {
      f = t->open_files->files[fd];
    }

  return f;
}

bool 
ptr_valid(void *esp, const void* ptr, int len, bool is_write)
{
  if (ptr + len < PHYS_BASE
   && ptr > USER_BOTTOM)
    {
      /* 
       * If operation is a read, then perform dummy reads on each page
       * that starts from (ptr) to (ptr+len). This will cause a page fault exception
       * and the page fault handler can take care of proper allocation.
       */

      /* 
       * If operation is a write, check first if SPTE is allocated and load 
       * the frames in. If no SPTE is found, check to see if the entire
       * memory locations from (ptr) to (ptr+len) is within the stack.
       * If it is, grow the stack accordingly.
       */
      const void* page_bottom;
      while (len >= 0) 
       {
         if (!is_write)
           {
             volatile int dummy = * (int*)(ptr);
           }

         else 
           {
             struct sup_pte *spte = get_spte(ptr);
             if (spte)
               {
                 if (!spte->writable)
                   {
                     exit(-1);
                   }

                 load_spte(spte);
               }

             else if (ptr > esp - 32)
               {
                 alloc_blank_spte (ptr);
               }

             else
               {
                 exit (-1);
               }
           }

         page_bottom = pg_round_up(ptr) + 1;
         len -= (page_bottom - ptr);
         ptr = page_bottom;
       }

      return true;
    }

  return false;
}

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&file_lock);
}

int
get_arg (void *esp, uint32_t *args, int num_args)
{
  if(!ptr_valid(esp, esp, num_args, PTR_READ)){return -1;}
  uint32_t *sp = (uint32_t *) esp;
  int i;
  for (i = 0; i < num_args; i++) 
  {
    sp += 1;
    if (is_user_vaddr ((const void *) sp))
      {
        args[i] = *sp;
      }
    else
      {
        return -1;
      }
  }
  return 1;
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  /* Validate stack pointer. */
  if(!ptr_valid(f->esp, f->esp, 0, PTR_READ))
    {
      exit(-1);
    }

  int sys_no = *((int *)f->esp);
  uint32_t args[3]; /* max args = 3 */

  switch (sys_no) 
    {
      case SYS_HALT:                   /* Halt the operating system. */
        {
          halt();
          break;
        }

      case SYS_EXIT:                   /* Terminate this process. */
        {
          if (get_arg(f->esp, args, 1) > 0)
            {
              exit (args[0]);
            }
          else
            {
              f->eax = -1;
              exit (-1);
            }
          break;
        }

      case SYS_EXEC:                   /* Start another process. */
        {
          if (get_arg(f->esp, args, 1) > 0)
            {
              const char *cmd = (const char *) (args[0]);
              f->eax = exec(cmd);
            }
          else
            {
              f->eax = -1;
            }
          break;
        }

      case SYS_WAIT:                   /* Wait for a child process to die. */
        {
          if (get_arg(f->esp, args, 1) > 0)
            {
              f->eax = wait(args[0]);
            }
          else
            {
              f->eax = -1;
            }
          break;
        }

      case SYS_CREATE:                 /* Create a file. */
        {
          if (get_arg(f->esp, args, 2) > 0)
           {
             unsigned size = (unsigned) args[1];
             const char* v_file = (const char*) args[0];
             if (v_file == NULL)
               {
                 exit (-1);
               }
          
             if (ptr_valid(f->esp, v_file, strlen(v_file), PTR_READ))
               {
                 f->eax = create(v_file, size);                 
               }
             else 
               {
                 f->eip = (void*) f->eax;
                 f->eax = -1;
                 exit(-1);
               }
            }     
          else 
            {
              f->eax = -1;
            }
          break;
        }
        
      case SYS_REMOVE:                 /* Delete a file. */
        {
          if (get_arg(f->esp, args, 1) > 0)
            {
              const char* v_file = (const char*) args[0];
              if (v_file == NULL)
                {
                  exit (-1);
								}
              if (ptr_valid(f->esp, v_file, strlen(v_file), PTR_READ))
                {
                  f->eax = remove(v_file);
                }
              else 
                {
                  f->eip = (void*) f->eax;
                  f->eax = -1;
                }
            }

          else  
            {
              f->eax = -1;
            }
          break;
        }

      case SYS_OPEN:                   /* Open a file. */
        {
          if (get_arg(f->esp, args, 1) > 0)
            {
              const char* v_file = (const char*) args[0];
              if (v_file == NULL)
                {
                  exit (-1);
                }
              if (ptr_valid(f->esp, v_file, strlen(v_file), PTR_READ))
                {
                  f->eax = open(v_file);
                }
              else 
                {
                  f->eip = (void*) f->eax;
                  f->eax = -1;
                  exit(-1);
                }
            }
          else 
            {
              f->eax = -1;
            }
          break;
        }

      case SYS_FILESIZE:               /* Obtain a file's size. */
        {
          if (get_arg(f->esp, args, 1) > 0)
            {
              int fd = (int) args[0];
              f->eax = filesize(fd);
            }
          else 
            {
              f->eax = -1;
            }
          break;
        }

      case SYS_READ:                   /* Read from a file. */
        {
          if (get_arg(f->esp, args, 3) > 0)
            {
              int fd = (int) args[0];
              void* v_buffer = (void*) args[1];
              unsigned size = (unsigned) args[2];
              if (ptr_valid(f->esp, v_buffer, size, PTR_WRITE))
                {
								  struct thread *t = thread_current(); 
									if(!pagedir_is_writable(t->pagedir, v_buffer))
										{
											f->eax = -1;
											exit(-1);
										} 
										
                  f->eax = read(fd, v_buffer, size);
                }
              else
                {
                  f->eip = (void*) f->eax;
                  f->eax = -1;
                  exit (-1);
                }
            }
          else 
            {
              f->eax = -1;
            }
          break;
        }

      case SYS_WRITE:                  /* Write to a file. */
        { 
          if (get_arg(f->esp, args, 3) > 0)
            {
              int fd = (int) args[0];
              const void* v_buffer = (void*) args[1];
              unsigned size = (unsigned) args[2];
              if (ptr_valid(f->esp, v_buffer, size, PTR_READ))
                {
                  f->eax = write(fd, v_buffer, size);
                }
              else 
                {
                  f->eip = (void*) f->eax;
                  f->eax = -1;
                  exit(-1);
                }
            }
          else 
            {
                f->eax = -1;
            }
          break;
        }

      case SYS_SEEK:                   /* Change position in a file. */
        {
          if (get_arg(f->esp, args, 2) > 0)
            {
              int fd = (int) args[0];
              unsigned pos = (unsigned) args[1];
              seek(fd, pos);
            }
          else 
            {
                f->eax = -1;
            }
          break;
        }

      case SYS_TELL:                   /* Report current position in a file. */
        {
          if (get_arg(f->esp, args, 1) > 0)
            {
              int fd = (int) args[0];
              f->eax = tell(fd);
            }
          else 
            {
              f->eax = -1;
            }
          break;
        }

      case SYS_CLOSE:                  /* Close a file. */
        {
          if (get_arg(f->esp, args, 2) > 0)
            {
              int fd = (int) args[0];
              close(fd);
            }
          else 
            {
                f->eax = -1;
            }
          break;
        }
      
      default:
        {
          f->eax = -1;
          thread_exit();
        }
    }
}


void
halt (void)
{
    /*
     * Terminates Pintos by calling shutdown_power_off() (declared in "threads/init.h").
     * This should be seldom used, because you lose some information about possible deadlock situations, etc. 
     */
  shutdown_power_off();
}

void
exit (int status) 
{
    /*
     * Terminates the current user program, returning status to the kernel.
     * If the process's parent waits for it (see below), this is the status that will be returned.
     * Conventionally, a status of 0 indicates success and nonzero values indicate errors. 
     */
  struct thread *curr = thread_current();
  struct thread *parent = thread_get (curr->parent_tid);
  if (parent)
    {
      struct child_process *cp_me = child_process_get (parent, curr->tid);
      cp_me->exit_status = status;
      sema_up(&(cp_me->exited));
    }

  printf ("%s: exit(%d)\n", curr->name, status);

  int i;
  for (i = 0; i < curr->open_files->size; i++)
    {
      if (curr->open_files->isOpen)
        {
          lock_acquire(&file_lock);
          file_close (curr->open_files->files[i]);
          lock_release(&file_lock);
        }
    }

  free (curr->open_files->files);
  free (curr->open_files->isOpen);
  free (curr->open_files);

  thread_exit();
}

pid_t
exec (const char *cmd_line) 
{
  /*
   * Runs the executable whose name is given in cmd_line, passing any given arguments, and returns the new process's ogram id (pid).
   * Must return pid -1, which otherwise should not be a valid pid, if the program cannot load or run for any reason.
   * Thus, the parent process cannot return from the exec until it knows whether the child process successfully loaded its executable.
   * You must use appropriate synchronization to ensure this. 
   */

  /* Check load is performed in process_execute() */
  pid_t pid = process_execute (cmd_line);
  return pid;
}

int
wait (pid_t pid) 
{
  /*
   * Waits for a child process pid and retrieves the child's exit status.
   *
   * If pid is still alive, waits until it terminates.
   * Then, returns the status that pid passed to exit.
   * If pid did not call exit(), but was terminated by the kernel (e.g. killed due to an exception), wait(pid) must turn -1.
   * It is perfectly legal for a parent process to wait for child processes that have already terminated by the time e parent calls wai
     * but the rnel must still allow the parent to retrieve its child's exit status, or learn that the child was terminated by t kernel.
    
   * wait must fail and return -1 immediately if any of the following conditions is true:
   *       pid does not refer to a direct child of the calling process.
   *       pid is a direct child of the calling process if and only if the calling process received pid as a return val from a successful call to exe
    
   * Note that children are not inherited: if A spawns child B and B spawns child process C, then A cannot wait for C, eveif B is dead.
   * A call to wait(C) by process A must fail.
   * Similarly, orphaned processes are not assigned to a new parent if their parent process exits before they do.
   *
   * The process that calls wait has already called wait on pid.
   * That is, a process may wait for any given child at most once. 
   *
   * Processes may spawn any number of children, wait for them in any order,
   * and may even exit without having waited for some or all of their children.
   * Your design should consider all the ways in which waits can occur.
   * All of a process's resources, including its struct thread, must be freed whether its parent ever waits for it or not,
   * and regardless of whether the child exits before or after its parent.
   *
   * You must ensure that Pintos does not terminate until the initial process exits.
   * The supplied Pintos code tries to do this by calling process_wait() (in "userprog/process.c") from main() (in "thads/init.c").
   * We suggest that you implement process_wait() according to the comment at the top of the function and then impment the wait system call in terms of process_wait().
   *
   * Implementing this system call requires considerably more work than any of the rest.
   */
  return process_wait (pid);
}

bool
create (const char *file, unsigned initial_size) 
{
    /*
     * Creates a new file called file initially initial_size bytes in size.
     * Returns true if successful, false otherwise.
     *
     * Creating a new file does not open it: opening the new file is a separate operation which would require a open system call. 
     */
  lock_acquire(&file_lock);
  bool success = filesys_create(file, initial_size);
  lock_release(&file_lock);
  return success;
}

bool
remove (const char *file) 
{
  /*
   * Deletes the file called file.
   * Returns true if successful, false otherwise.
   *
   * A file may be removed regardless of whether it is open or closed, and removing an open file does not close it.
   * See Removing an Open File, for details. 
   */
  lock_acquire(&file_lock);
  /* Implementation not complete. */
  bool success = filesys_remove(file);
  lock_release(&file_lock);
  return success;
}

int
open (const char *file) 
{
  /*
   * Opens the file called file.
   * Returns a nonnegative integer handle called a "file descriptor" (fd), or -1 if the file could not be opened.
   *
   * File descriptors numbered 0 and 1 are reserved for the console:
   *     fd 0 (STDIN_FILENO) is standard input, fd 1 (STDOUT_FILENO) is standard output.
   * The open system call will never return either of these file descriptors,
   * which are valid as system call arguments only as explicitly described below.
   *
   * Each process has an independent set of file descriptors.
   * File descriptors are not inherited by child processes.
   *
   * When a single file is opened more than once, whether by a single process or different processes, each open returns a new file descriptor.
   * Different file descriptors for a single file are closed independently in separate calls to close and they do not share a file position.
   */
  int fd; /* Return value. */
  
  lock_acquire(&file_lock);
  struct file* f = filesys_open(file);
  lock_release(&file_lock);
  // printf("open\n");

  if (f == NULL)
    {
      fd = -1;
    }
  
  else 
    {
      /* File opened, add to thread's open files. */
      struct thread* t = thread_current();
      // printf("open files: %d\n", t->open_files->size);

      /* Check for an open fd space in current thread's file list. */
      /* Could also be made easier if there was a variable keeping track
       * of how many files were open for the thread. */
      int i;
      bool foundHole = false;
      for (i = 0; i < t->open_files->size; i++)
        {
          if (!t->open_files->isOpen[i])
            {
              /* Open space found. Add here. */
              foundHole = true;
              fd = i;
              break;
            }
        }

      /* No holes avaliable in open file list. Append to file list. */
      if (!foundHole)
        {
          /* Allocate more memory for the file list. */
          t->open_files->size++;
          t->open_files->files = (struct file**) 
              realloc(t->open_files->files, sizeof(struct file*) * t->open_files->size);
          t->open_files->isOpen = (bool*) 
              realloc(t->open_files->isOpen, sizeof(bool) * t->open_files->size);

          fd = t->open_files->size - 1;
        }

      t->open_files->files[fd] = f;
      t->open_files->isOpen[fd] = true;
      
      /* Determine if this is an ELF file. If so, deny write access. */
      lock_acquire(&file_lock);
	  //printf("File name wanted to open: %s; Thread name: %s\n", file, t->name);
      //if (strcmp(file, t->name) == 0)
      if(is_ELF(f, (char *) file))  
        {
          file_deny_write(f);
        }
      file_seek(f, 0);
      lock_release(&file_lock);
    }
  
  return fd;
}

int
filesize (int fd) 
{
  /*
   * Returns the size, in bytes, of the file open as fd. 
   */
  int size;
  struct thread* t = thread_current();

  /* Check for valid fd. */
  if (!is_open(t , fd))
    {
      size = -1;
    }
  else
    {
      lock_acquire(&file_lock);
      size = file_length(fd_to_file(t, fd));
      lock_release(&file_lock);
    }
 return size;
}

int
read (int fd, void *buffer, unsigned size) 
{
  /*
   * Reads size bytes from the file open as fd into buffer.
   * Returns the number of bytes actually read (0 at end of file),
   * or -1 if the file could not be read (due to a condition other than end of file).
   *
   * Fd 0 reads from the keyboard using input_getc(). 
   */
  /* Return value. */
  int bytes_read;
  /* Cannot read from STDOUT. */
  if (fd == 1) 
    {
      bytes_read = -1;
    }

  /* Handle STDIN. */
  else if (fd == 0)
    {
      /* Read in 'size' number of characters form the console. */
      char* buf = (char*) buffer;
      unsigned i;
      for (i = 0; i < size; i++)
        {
          buf[i] = input_getc();
        }
      /* Null termination. */
      buf[size] = 0;
      bytes_read = size;
   }

  else
    {
      /* Read from file. */
      struct file* f = fd_to_file(thread_current(), fd);
      if (f == NULL)
        {
          bytes_read = -1;
        }
      else
        {
          lock_acquire(&file_lock);
          bytes_read = (int) file_read(f, buffer, size);
          lock_release(&file_lock);
        }
    }
  return bytes_read;
}

int
write (int fd, const void *buffer, unsigned size)
{
  /*
   * Writes size bytes from buffer to the open file fd.
   * Returns the number of bytes actually written, which may be less than size if some bytes could not be written.
   *
   * Writing past end-of-file would normally extend the file, but file growth is not implemented by the basic file system.
   * The expected behavior is to write as many bytes as possible up to end-of-file and return the actual number written, or 0 if no bytes could be written at all.
   *
   * Fd 1 writes to the console.
   * Your code to write to the console should write all of buffer in one call to putbuf(),
   * at least as long as size is not bigger than a few hundred bytes.  (It is reasonable to break up larger buffers.)
   * Otherwise, lines of text output by different processes may end up interleaved on the console,
   * confusing both human readers and our grading scripts.
   */
  int bytes_written;

  /* Cannot write to STDIN. */
  if (fd == 0)
    {
      bytes_written = -1;
    }

  /* Handle STDOUT. */
  else if (fd == 1)
    {
      putbuf(buffer, size);
      bytes_written = size;
    }    

  /* Write to file. */
  else 
    {
      struct file* f = fd_to_file(thread_current(), fd);
      if (f == NULL)
        {
          bytes_written = -1;
        }
      else 
        {
          lock_acquire(&file_lock);
          bytes_written = (int) file_write(f, buffer, size);
          lock_release(&file_lock);
        }
     }
  return bytes_written;
}

void
seek (int fd, unsigned position) 
{
  /*
   * Changes the next byte to be read or written in open file fd to position,
   * expressed in bytes from the beginning of the file. (Thus, a position of 0 is the file's start.)
   *
   * A seek past the current end of a file is not an error.
   * A later read obtains 0 bytes, indicating end of file.
   * A later write extends the file, filling any unwritten gap with zeros.
   * (However, in Pintos files have a fixed length until project 4 is complete, so writes past end of file will return an error.)
   * These semantics are implemented in the file system and do not require any special effort in system call implementation.
   */
  struct thread* t = thread_current();
  if (is_open(t, fd))
    {
      lock_acquire(&file_lock);
      file_seek(fd_to_file(t, fd), position);
      lock_release(&file_lock);  
    }
}

unsigned
tell (int fd) 
{
  /*
   * Returns the position of the next byte to be read or written in open file fd, expressed in bytes from the beginning of the file. 
   */
  unsigned pos;
  struct thread* t = thread_current();
  if (!is_open(t, fd))
    {
      pos = -1;
    }
  else 
    {
      lock_acquire(&file_lock);
      pos = file_tell(fd_to_file(t, fd));
      lock_release(&file_lock); 
    }
  return pos; 
}

void
close (int fd) 
{
  /*
   * Closes file descriptor fd.
   * Exiting or terminating a process implicitly closes all its open file descriptors, as if by calling this function for each one. 
   */
  struct thread* t = thread_current();
  /* Check fd validity. */
  if (is_open(t, fd))
    {
      lock_acquire(&file_lock);
      file_close(fd_to_file(t, fd));
      lock_release(&file_lock);
      
      t->open_files->files[fd] = NULL;
      t->open_files->isOpen[fd] = false; 
    }
}
