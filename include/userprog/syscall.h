#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include "threads/synch.h"

void syscall_init (void);

void halt (void);
void exit (int status);
int fork (const char *thread_name);

struct lock file_lock;
struct thread *get_child_process (int pid);
#endif /* userprog/syscall.h */
