# Project 4: Process Memory Allocation

Memory management is one of the operating systems’ primary responsibilities. When programs are running on a computer, the operating system must allocate memory to programs, track allocated regions, and free it later.  Operating systems manage memory in part by creating a page table for each program. 

In this assignment, I created a kernel module that handles allocation and free requests from a process (similar to mmap and munmap).

##### Note
Based on source code from [cse330-public](https://github.com/ASTERISC-Teaching/cse330-public.git), provided in CSE330: Operating Systems at ASU.