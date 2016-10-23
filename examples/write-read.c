#include <stdio.h>
#include <syscall.h>

static char buf[8192] = "111111111";
static char buf2[8192] = "222222222";

int main (int argc, char *argv[])
{

  int seek_val = 4093;

  create("test.txt", 8192);
  int fd_lol = open("test.txt");
  int fd = open("test.txt");

  seek(fd, seek_val);
  int bytes_written = write(fd, "hello\n\0", 7);

  printf("Bytes written: %d\n", bytes_written);
  printf("Before read buffer value: %s\n", buf);
  seek(fd, seek_val);
  int bytes_read = read(fd, &buf[0], 6);
  printf("File contents: %s\n", buf);
  printf("Bytes read: %d\n", bytes_read);

  close(fd);

  int fd2 = open("test.txt");
  seek(fd2, seek_val);
  read(fd2, &buf2[0], 6);
  printf("File contents after close: %s\n", buf2);
  close(fd2);

  return 0;
}
