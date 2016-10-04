#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/synch.h"

typedef int pid_t;

struct child_process
  {
    pid_t pid;

    int load_status;
    struct semaphore loaded;

    int exit_status;
    struct semaphore exited;

    struct list_elem elem;
  };

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

bool is_ELF(struct file* file, char *file_name);

struct child_process *child_process_init(pid_t pid);
void child_process_free (struct child_process *cp);
struct child_process *child_process_get(struct thread *parent, pid_t child_pid);

#endif /* userprog/process.h */
