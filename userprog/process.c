#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "userprog/process.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "lib/string.h"

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);
void push_to_stack(void **stack_ptr, void *src, int size);

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. 
   
   Order for process execute:

   Kernel calls process_execute (or exec) which passes in the entire
   command line of the filename folowed by args. process_execute
   makes a call to thread_create which sets up a thread with the
   function as start_process and arguments as fn_copy. The thread
   then calls start_process once its ready to star running which
   then calls load which calls setup_stack which sets up the init
   stage of the stack. When start_process is called, we are already
   in the child thread. 

    Proof: Check TID of current thread and TID of start_process
    thread.
   */
tid_t
process_execute (const char *file_name) 
{
  char *fn_copy;
  tid_t tid;
  struct thread *curr_thread = thread_current();

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);

  /* Create a new thread to execute FILE_NAME. */
  /* printf("(process_execute) address of file_name: %p\n", file_name); */
  /* printf("(process_execute) address of fn_copy: %p\n", fn_copy); */
  tid = thread_create (file_name, PRI_DEFAULT, start_process, fn_copy);
  if (tid == TID_ERROR)
    {
      palloc_free_page (fn_copy); 
    }
  else
    {
      struct list_elem *child_elem = list_back(&(curr_thread->child_processes));
      struct child_process *child = list_entry(child_elem, struct child_process, elem);
      
      sema_down(&(child->loaded));
      if (child->load_status < 0) 
        {
          /* fn_copy will have been freed in start_process() at this point */
          list_remove (child_elem);
          child_process_free (child);
          tid = TID_ERROR; 
        }
    }

  /* printf("process_execute() in thread #%d\n", curr_thread->tid); */
  /* printf("process_execute() created thread #%d\n", tid); */
  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;

  /*
  struct thread *curr_thread;
  curr_thread = thread_current();
  printf("start_process() in thread #%d\n", curr_thread->tid); 
  printf("(start_process) address pointed to by file_name: %p\n", file_name); 
  */

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (file_name, &if_.eip, &if_.esp);

  /* If load failed, quit. */
  palloc_free_page (file_name);
  if (!success) 
    thread_exit ();

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid UNUSED) 
{
  struct thread *curr = thread_current();
  struct child_process *child = child_process_get (curr, child_tid);
  int status;

  if (!child)
    { 
      return -1;
    }
  sema_down (&child->exited);
  status = child->exit_status;

  list_remove (&(child->elem));
  child_process_free (child);

  return status;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp, const char *args);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *args, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;
  char *token, *args_copy, *save_ptr;

  struct thread *parent = thread_get(t->parent_tid);
  struct list_elem *e = list_back(&(parent->child_processes));
  struct child_process *me = list_entry(e, struct child_process, elem);

  args_copy = (char *)(malloc((strlen(args) + 1) * sizeof(char)));
  strlcpy(args_copy, args, strlen(args)+1);

  /*
  printf("(load) address of args: %p\n", args);
  printf("(load) address of args_copy: %p\n", args_copy);
  printf("(load) value of args: %s\n", args);
  printf("(load) value of args_copy: %s\n", args_copy);
  */

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
  {
    /* printf("error loc 1\n"); */
    goto done;
  }
  process_activate ();

  /* Open executable file. */
  token = strtok_r (args_copy, " ", &save_ptr); 
  /* printf("executable name: %s\n", token); */
  file = filesys_open (token);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", token);
      /* printf("error loc 2\n"); */
      goto done; 
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", token);
      /* printf("error loc 3\n"); */
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
      {
        /* printf("error loc 4\n"); */
        goto done;
      }
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
      {
        /* printf("error loc 5\n"); */
        goto done;
      }
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          /* printf("error loc 6\n"); */
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
              {
                /* printf("error loc 7\n"); */
                goto done;
              }
            }
          else 
          {
            /* printf("error loc 8\n"); */
            goto done;
          }
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp, args)) 
  {
    /* printf("error loc 9\n"); */
    goto done;
  }

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done: /* We arrive here whether the load is successful or not. */
  if (success)
    {
      me->load_status = 1;
      /* printf ("load successful\n"); */
    }
  else
    {
      me->load_status = -1;
      /* printf ("load unsuccessful\n"); */
    }
  sema_up(&(me->loaded));

  free (args_copy);
  file_close (file);
  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable)) 
        {
          palloc_free_page (kpage);
          return false; 
        }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

void
push_to_stack(void **stack_ptr, void *src, int size)
{
  *stack_ptr -= size;
  memcpy(*stack_ptr, src, size);
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. 
   PHYS_BASE - 12 gives us room for return addr, argv, argc*/
static bool
setup_stack (void **esp, const char *args) 
{
  uint8_t *kpage;
  bool success = false;

  /* printf("we setting up stack!\n"); */

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
        {
          *esp = PHYS_BASE;
        }
      else
        {
          palloc_free_page (kpage);
          return 0;
        }
    }

  char *args_copy, *token, *save_ptr;
  int argc, argv_cap;
  char **argv;

  /* Keep a local copy of the command line arguments */
  args_copy = (char *)(malloc((strlen(args) + 1) * sizeof(char)));
  strlcpy(args_copy, args, strlen(args)+1);

  /* 
  printf("(setup_stack) address of args: %p\n", args);
  printf("(setup_stack) address of args_copy: %p\n", args_copy);
  printf("(setup_stack) value of args: %s\n", args);
  printf("(setup_stack) value of args_copy: %s\n", args_copy);
  */

  /* Initialize argv
   * Array to store starting addresses of arguments placed on stack
   */
  argc = 0; /* counter for number of arguments added to stack */
  argv_cap = 4; /* capacity of argv */
  argv = (char **)(malloc(argv_cap * sizeof(char *)));

  /* Copy arguments on stack 
   * and keep track of their starting addresses
   */
  for (token = strtok_r (args_copy, " ", &save_ptr); token != NULL;
       token = strtok_r (NULL, " ", &save_ptr))
    {
      push_to_stack(esp, token, strlen(token) + 1);

      argv[argc] = *esp;
      argc++;

      if (argc == argv_cap) /* Resize argv */
        {
          argv_cap <<= 1;
          argv = (char **)(realloc(argv, argv_cap * sizeof(char *)));
        }
    }
  argv[argc] = 0; /* Null termination */

  /* Align to word size (4 bytes) */
  int align;
  for (align = (size_t) *esp % 4; align > 0; align--)
    {
      /* printf("align byte: %d\n", align); */
      push_to_stack(esp, &argv[argc], sizeof(uint8_t));
    }

  /* Push argv[i] for all i */
  int i;
  for (i = argc; i >= 0; i--)
    {
      push_to_stack(esp, &argv[i], sizeof(char *));
    }

  char *temp = *esp;
  push_to_stack(esp, &temp, sizeof(char *)); /* Push &argv[0] */
  push_to_stack(esp, &argc, sizeof(int));    /* Push argc */
  push_to_stack(esp, temp, sizeof(void *));  /* Push fake return addr */

  /* print stack
  char *p; 
  for (p = (char *)(*esp); p <= (char *)PHYS_BASE; p++)
    {
      printf("%p: %X\n", p, *p); 
    }
  */

  free (argv);
  free (args_copy);
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

struct child_process *
child_process_init (pid_t pid)
{
  struct child_process *new_child = (struct child_process *) (malloc (sizeof (struct child_process)));
  new_child->pid = pid;
  new_child->exit_status = 0;
  new_child->load_status = 0;
  sema_init (&(new_child->exited), 0);
  sema_init (&(new_child->loaded), 0);

  return new_child;
}

void 
child_process_free (struct child_process *cp)
{
  free (cp);
}

struct child_process *
child_process_get (struct thread *parent, pid_t child_pid)
{
  struct list *cp = &(parent->child_processes);
  struct list_elem *e;

  for (e = list_begin (cp); e != list_end (cp);
       e = list_next (e))
    {
      struct child_process *c = list_entry (e, struct child_process, elem);
      if (c->pid == child_pid)
        {
          return c;
        }
    }

  return NULL;
}

/* This function returns a boolean determining if the passed file is an ELF executable. */
bool is_ELF(struct file* file){
    struct Elf32_Ehdr ehdr;
    if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
        || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
        || ehdr.e_type != 2
        || ehdr.e_machine != 3
        || ehdr.e_version != 1
        || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
        || ehdr.e_phnum > 1024) 
    {
        /* Not an ELF file. */
        return false;
    }
    return true;
 
}
