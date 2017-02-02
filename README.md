# Pintos

Our solution of [Pintos][main] for EE 461S: Operating Systems Fall 2016. It should be noted that the solution for [threads][br_threads] and [filesys][br_filesys] are under their own branches in order to remove their interactions with userprog and vm (master). This change, along with other new requirements, are made under the disrection of our professor.

### What Was Actually Implemented
1. [Threads][threads]
  2. Synchronization using semaphores, locks, and condition variables
  2. Remove busy-wait loops by allowing threads to sleep
  2. Priority scheduling with priority donation
  
1. [User Programs][userprog]
  2. Argument passing to new processes
  2. System calls: 
  `halt, exit, exec, wait, create, remove, open, filesize, read, write, seek, tell, close`
  
1. [Virtual Memory][vm]
  2. Manage user virtual pages and physical frames using page tables and frame tables
  2. Create a swap disk
  2. Allow for file lazy loading and stack growth
  
1. [File Systems][filesys]
  2. Files are extensible (upto 8MB)
  2. Subdirectories
  2. System calls: `chdir, mkdir, readdir, isdir. inumber`


<!-- Links -->
[main]: https://web.stanford.edu/class/cs140/projects/pintos/pintos_1.html#SEC1
[threads]: https://web.stanford.edu/class/cs140/projects/pintos/pintos_2.html#SEC15
[userprog]: https://web.stanford.edu/class/cs140/projects/pintos/pintos_3.html#SEC32
[vm]: https://web.stanford.edu/class/cs140/projects/pintos/pintos_4.html#SEC53
[filesys]: https://web.stanford.edu/class/cs140/projects/pintos/pintos_5.html#SEC75
[br_threads]: https://github.com/qbaula/project2/tree/threads
[br_filesys]: https://github.com/qbaula/project2/tree/filesys
