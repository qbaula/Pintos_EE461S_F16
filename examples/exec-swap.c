#include <stdio.h>
#include <syscall.h>
#include <stdlib.h>

#define MEM_PER_CHILD (1024)

static char buf[MEM_PER_CHILD];
static char children[50];

void alloc_memory (char *buf, int num_bytes, int value);

int
main (int argc, char *argv[])
{ 
  int num_child;
  printf("ARGC %d\n", argc);
  if(argc == 2)
    {
      printf("ARGV %s %s\n", argv[0], argv[1]);
      num_child = atoi(argv[1]);
    } 
  else 
    {
      num_child = 0;
    }

  if (num_child == 0)
    {
      alloc_memory (&buf[0], MEM_PER_CHILD, 0);
      return 0x40;
    }
  else
    {
      int i;
      for (i = 0; i < num_child; i++)
        {
          children[i] = exec ("exec-swap");
          printf("Children PID: %d\n", children[i]);
        }

      /*
      for (i = 0; i < num_child; i++)
        {
          //printf("child %d exit with %d\n", i, wait (children[i]));
        }
        */
    }

  return 0x20;
}

void
alloc_memory (char *buf, int num_bytes, int value)
{
  int i;
  for (i = 0; i < num_bytes; i++)
    {
      buf[i] = value;
    }
  printf("fully allocated\n");
}
