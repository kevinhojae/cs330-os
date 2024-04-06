#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	// printf ("system call!\n");

	// 1. Extract system call number and arguments from f->RAX and other registers.
	// when the system call handler syscall_handler() gets control, the system call number is in the rax, and arguments are passed with the order %rdi, %rsi, %rdx, %r10, %r8, and %r9.
	int syscall_number = f->R.rax;
	int arg1 = f->R.rdi;
	int arg2 = f->R.rsi;
	int arg3 = f->R.rdx;
	int arg4 = f->R.r10;
	int arg5 = f->R.r8;
	int arg6 = f->R.r9;

	// 2. Switch based on system call number to handle different system calls.
	// 3. For system calls involving user pointers, validate pointers before proceeding.
	// 4. Perform the requested operation, which may involve interacting with the file system, process management, or memory management subsystems.
	// 5. Return results to the calling process, either through the return value in f->RAX or through state changes in the process or system.
	// If a system call is passed an invalid argument, acceptable options include returning an error value (for those calls that return a value), returning an undefined value, or terminating the process.
	switch (syscall_number) {
		// syscall-nr is 0
		case SYS_HALT:
			halt ();
			break;
		// syscall-nr is 1
		case SYS_EXIT:
			exit (arg1);
			break;
		case SYS_FORK:
			break;                   /* Clone current process. */
		case SYS_EXEC:
			break;                   /* Switch current process. */
		case SYS_WAIT:
			break;                   /* Wait for a child process to die. */
		case SYS_CREATE: 
			break;                /* Create a file. */
		case SYS_REMOVE:
			break;                 /* Delete a file. */
		case SYS_OPEN:
			break;                   /* Open a file. */
		case SYS_FILESIZE:
			break;               /* Obtain a file's size. */
		case SYS_READ:
			break;                   /* Read from a file. */
		// case for write
		case SYS_WRITE:
			return write (arg1, (void *) arg2, (unsigned) arg3);
			break;
		case SYS_SEEK:
			break;                   /* Change position in a file. */
		case SYS_TELL:
			break;                   /* Report current position in a file. */
		case SYS_CLOSE:
			break;                  /* Close a file. */
		default:
			thread_exit ();
			break;
	}
}

/**
 * Terminates Pintos by calling power_off() (declared in src/include/threads/init.h). This should be seldom used, because you lose some information about possible deadlock situations, etc.
 */
void
halt (void) {
	// TODO: implement kernel logic for halt
	power_off ();
}

/**
 * Terminates the current user program, returning status to the kernel. If the process's parent waits for it (see below), this is the status that will be returned.
 * Conventionally, a status of 0 indicates success and nonzero values indicate errors.
 */
void
exit (int status) {
	// TODO: implement kernel logic for exit
	/*
	// syscall-nr is 1
	case SYS_EXIT:
		exit (arg1);
		break;
	*/
	struct thread *curr_thread = thread_current (); // 현재 쓰레드 받기
	curr_thread-> exit_status = status; // 현재 쓰레드의 exit_status에 인수로 받은status 저장
	printf("%s: exit(%d)\n", curr_thread->name, status); // 현재 쓰레드의 이름과 status 출력
	thread_exit ();	// 현재 쓰레드 종료
}

/**
 * Create new process which is the clone of current process with the name THREAD_NAME.
 * You don't need to clone the value of the registers except %RBX, %RSP, %RBP, and %R12 - %R15, which are callee-saved registers. Must return pid of the child process, otherwise shouldn't be a valid pid. 
 * In child process, the return value should be 0. The child should have DUPLICATED resources including file descriptor and virtual memory space. Parent process should never return from the fork until it knows whether the child process successfully cloned. That is, if the child process fail to duplicate the resource, the fork () call of parent should return the TID_ERROR.
 * The template utilizes the pml4_for_each() in threads/mmu.c to copy entire user memory space, including corresponding pagetable structures, but you need to fill missing parts of passed pte_for_each_func (See virtual address).
 */
int
fork (const char *thread_name) {
	// TODO: implement kernel logic for fork
}

/**
 * Change current process to the executable whose name is given in cmd_line, passing any given arguments.
 * This never returns if successful. Otherwise the process terminates with exit state -1, if the program cannot load or run for any reason.
 * This function does not change the name of the thread that called exec. Please note that file descriptors remain open across an exec call.
 */
int
exec (const char *cmd_line) {
	// TODO: implement kernel logic for exec
}

/**
 * Waits for a child process pid and retrieves the child's exit status. If pid is still alive, waits until it terminates. Then, returns the status that pid passed to exit. 
 * If pid did not call exit(), but was terminated by the kernel (e.g. killed due to an exception), wait(pid) must return -1. It is perfectly legal for a parent process to wait for child processes that have already terminated by the time the parent calls wait, but the kernel must still allow the parent to retrieve its child’s exit status, or learn that the child was terminated by the kernel.
 * 
 * wait must fail and return -1 immediately if any of the following conditions is true:
 * 	- pid does not refer to a direct child of the calling process. pid is a direct child of the calling process if and only if the calling process received pid as a return value from a successful call to fork. Note that children are not inherited: if A spawns child B and B spawns child process C, then A cannot wait for C, even if B is dead. A call to wait(C) by process A must fail. Similarly, orphaned processes are not assigned to a new parent if their parent process exits before they do.
 * 	- The process that calls wait has already called wait on pid. That is, a process may wait for any given child at most once.
 */
int
wait (int pid) {
	// TODO: implement kernel logic for wait
}

/**
 * Creates a new file called file initially initial_size bytes in size. Returns true if successful, false otherwise.
 * Creating a new file does not open it: opening the new file is a separate operation which would require a open system call.
 */
bool
create (const char *file, unsigned initial_size) {
	// TODO: implement kernel logic for create
}

/**
 * Deletes the file called file. Returns true if successful, false otherwise.
 * A file may be removed regardless of whether it is open or closed, and removing an open file does not close it. See Removing an Open File in FAQ for details.
 */
bool
remove (const char *file) {
	// TODO: implement kernel logic for remove
}

/**
 * Opens the file called file. Returns a nonnegative integer handle called a "file descriptor" (fd), or -1 if the file could not be opened.
 * File descriptors numbered 0 and 1 are reserved for the console: fd 0 (STDIN_FILENO) is standard input, fd 1 (STDOUT_FILENO) is standard output.
 * The open system call will never return either of these file descriptors, which are valid as system call arguments only as explicitly described below. Each process has an independent set of file descriptors. File descriptors are inherited by child processes. When a single file is opened more than once, whether by a single process or different processes, each open returns a new file descriptor.
 * Different file descriptors for a single file are closed independently in separate calls to close and they do not share a file position. You should follow the linux scheme, which returns integer starting from zero, to do the extra.
 */
int
open (const char *file) {
	// TODO: implement kernel logic for open
}

/**
 * Returns the size, in bytes, of the file open as fd.
 */
int
filesize (int fd) {
	// TODO: implement kernel logic for filesize
}

/**
 * Reads size bytes from the file open as fd into buffer. Returns the number of bytes actually read (0 at end of file), or -1 if the file could not be read (due to a condition other than end of file).
 * fd 0 reads from the keyboard using input_getc().
 */
int
read (int fd, void *buffer, unsigned size) {
	// TODO: implement kernel logic for read
}

/**
 * Writes size bytes from buffer to the open file fd. Returns the number of bytes actually written, which may be less than size if some bytes could not be written.
 * Writing past end-of-file would normally extend the file, but file growth is not implemented by the basic file system. The expected behavior is to write as many bytes as possible up to end-of-file and return the actual number written, or 0 if no bytes could be written at all.
 * fd 1 writes to the console. Your code to write to the console should write all of buffer in one call to putbuf(), at least as long as size is not bigger than a few hundred bytes (It is reasonable to break up larger buffers). Otherwise, lines of text output by different processes may end up interleaved on the console, confusing both human readers and our grading scripts.
 */
int
write (int fd, const void *buffer, unsigned size) {
	struct lock *file_lock;
	// TODO: implement kernel logic for write

	// write to console
	if (fd == 1) {
		putbuf (buffer, size);
		return size;
	}

	// write to file
	struct file *file = file_open (fd);
	if (file != NULL) {
		lock_acquire(&file_lock);
		int bytes_written = file_write(file, buffer, size);
		lock_release(&file_lock);
		return bytes_written;
	}
	return -1; // file descriptor not found or file is not open
}

/**
 * Changes the next byte to be read or written in open file fd to position, expressed in bytes from the beginning of the file (Thus, a position of 0 is the file's start). A seek past the current end of a file is not an error. A later read obtains 0 bytes, indicating end of file. A later write extends the file, filling any unwritten gap with zeros. (However, in Pintos files have a fixed length until project 4 is complete, so writes past end of file will return an error.) These semantics are implemented in the file system and do not require any special effort in system call implementation.
 */
void
seek (int fd, unsigned position) {
	// TODO: implement kernel logic for seek
}

/**
 * Returns the position of the next byte to be read or written in open file fd, expressed in bytes from the beginning of the file.
 */
unsigned
tell (int fd) {
	// TODO: implement kernel logic for tell
}

/**
 * Closes file descriptor fd. Exiting or terminating a process implicitly closes all its open file descriptors, as if by calling this function for each one.
 */
void
close (int fd) {
	// TODO: implement kernel logic for close
}
