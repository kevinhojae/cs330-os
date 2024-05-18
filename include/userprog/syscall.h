#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);

void exit_handler (int status);

struct lock file_lock;

#endif /* userprog/syscall.h */
