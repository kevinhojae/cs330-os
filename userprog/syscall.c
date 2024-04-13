#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/input.h"
#include "threads/synch.h"
#include "threads/init.h"
#include "userprog/process.h"
#include "lib/user/syscall.h"
#include "threads/palloc.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

static void halt_handler (void);
static int fork_handler (const char *thread_name, struct intr_frame *f);
static int exec_handler (const char *cmd_line);
static int wait_handler (int pid);
static bool create_handler (const char *file, unsigned initial_size);
static bool remove_handler (const char *file);
static int open_handler (const char *file_name);
static int filesize_handler (int fd);
static int read_handler (int fd, void *buffer, unsigned size);
static int write_handler (int fd, const void *buffer, unsigned size);
static void seek_handler (int fd, unsigned position);
static unsigned tell_handler (int fd);
static void close_handler (int fd);
static struct file *get_file_from_fd_table (int fd);
static int add_file_to_fd_table (struct file *file);
static bool remove_file_from_fd_table (int fd);
static void validate_address_range (const void *addr, unsigned size);
static void validate_string_range (const char *addr);

struct lock file_lock;
struct lock syscall_lock;

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

	next_fd = 2; // 0 is STDIN_FILENO, 1 is STDOUT_FILENO
	lock_init(&file_lock);
	lock_init(&syscall_lock);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	// printf ("system call!\n");

	// 1. Extract system call number and arguments from f->RAX and other registers.
	// 2. Switch based on system call number to handle different system calls.
	// 3. For system calls involving user pointers, validate pointers before proceeding.
	// 4. Perform the requested operation, which may involve interacting with the file system, process management, or memory management subsystems.
	// 5. Return results to the calling process, either through the return value in f->RAX or through state changes in the process or system.
	// If a system call is passed an invalid argument, acceptable options include returning an error value (for those calls that return a value), returning an undefined value, or terminating the process.
	switch (f->R.rax) {
		case SYS_HALT:  		   		/* Halt the operating system. */
			halt_handler ();
			break;
		case SYS_EXIT:  		 		/* Terminate this process. */
			exit_handler (f->R.rdi);
			break;
		case SYS_FORK: 		 			/* Clone current process. */
			f->R.rax = fork_handler ((const char *) f->R.rdi, f);
			break;                   
		case SYS_EXEC: 
			f->R.rax = exec_handler((const char *) f->R.rdi);
			break;
		case SYS_WAIT: 		 			/* Wait for a child process to die. */
			f->R.rax = wait_handler (f->R.rdi);
			break;
		case SYS_CREATE:  				/* Create a file. */
			f->R.rax = create_handler ((const char *) f->R.rdi, (unsigned) f->R.rsi);
			break;
		case SYS_REMOVE: 				/* Delete a file. */
			f->R.rax = remove_handler ((const char *) f->R.rdi);
			break;
		case SYS_OPEN:  				/* Open a file. */
			f->R.rax = open_handler ((const char *) f->R.rdi);
			break;
		case SYS_FILESIZE: 				/* Obtain a file's size. */
			f->R.rax = filesize_handler (f->R.rdi);
			break;
		case SYS_READ: 					/* Read from a file. */
			f->R.rax = read_handler (f->R.rdi, (void *) f->R.rsi, (unsigned) f->R.rdx);
			break;
		case SYS_WRITE: 				/* Write to a file. */
			f->R.rax = write_handler (f->R.rdi, (const void *) f->R.rsi, (unsigned) f->R.rdx);
			break;
		case SYS_SEEK: 					/* Change position in a file. */
			seek_handler (f->R.rdi, (unsigned) f->R.rsi);
			break;
		case SYS_TELL: 					/* Report current position in a file. */
			f->R.rax = tell_handler (f->R.rdi);
			break;
		case SYS_CLOSE: 				/* Close a file. */
			close_handler (f->R.rdi);
			break;
		default:
			// thread_exit ();
			exit_handler (-1);
			break;
	}
}

/**
 * Terminates Pintos by calling power_off() (declared in src/include/threads/init.h). This should be seldom used, because you lose some information about possible deadlock situations, etc.
 */
void
halt_handler (void) {
	power_off ();
}

/**
 * Terminates the current user program, returning status to the kernel. If the process's parent waits for it (see below), this is the status that will be returned.
 * Conventionally, a status of 0 indicates success and nonzero values indicate errors.
 */
void
exit_handler (int status) {
	struct thread *curr_thread = thread_current (); // 현재 쓰레드 받기
	curr_thread->exit_status = status; // 현재 쓰레드의 exit_status에 인수로 받은 status 저장

	printf("%s: exit(%d)\n", curr_thread->name, status);
	thread_exit ();	// 현재 쓰레드 종료
}

/**
 * Create new process which is the clone of current process with the name THREAD_NAME.
 * You don't need to clone the value of the registers except %RBX, %RSP, %RBP, and %R12 - %R15, which are callee-saved registers.
 * Must return pid of the child process, otherwise shouldn't be a valid pid. 
 * In child process, the return value should be 0. The child should have DUPLICATED resources including file descriptor and virtual memory space.
 * Parent process should never return from the fork until it knows whether the child process successfully cloned. That is, if the child process fail to duplicate the resource, the fork () call of parent should return the TID_ERROR.
 * The template utilizes the pml4_for_each() in threads/mmu.c to copy entire user memory space, including corresponding pagetable structures, but you need to fill missing parts of passed pte_for_each_func (See virtual address).
 */
int
fork_handler (const char *thread_name, struct intr_frame *f) {
	validate_string_range (thread_name);

	// fork는 메모리를 복사하는 과정이기에 syscall_lock을 사용
	lock_acquire (&syscall_lock);
	pid_t child_pid = process_fork (thread_name, f);
	lock_release (&syscall_lock);

	if (child_pid == TID_ERROR) {
		return TID_ERROR;
	}

	struct thread *child_proc = get_child_process (child_pid);
	if (child_proc == NULL) {
		return TID_ERROR;
	}

	// wait until child process is loaded
	sema_down (&child_proc->sema_load);
	if (child_proc->exit_status == TID_ERROR) {
		return TID_ERROR;
	}

	return child_pid;
}

/**
 * Change current process to the executable whose name is given in cmd_line, passing any given arguments.
 * This never returns if successful. Otherwise the process terminates with exit state -1, if the program cannot load or run for any reason.
 * This function does not change the name of the thread that called exec. Please note that file descriptors remain open across an exec call.
 */
int
exec_handler (const char *cmd_line) {
	validate_string_range (cmd_line);

	if (cmd_line == NULL) { // cmd_line이 NULL이면 종료
		exit_handler (-1);
	}

	// make copy of cmd_line
	// char *cmd_line_copy 선언. palloc_get_multiple 이용
	// strlen(cmd_line) + 1 : 문자열 + null 종료 문자 크기
	// +(PGSIZE-1) 이후 /(PGSIZE) : 필요한 페이지 수 올림 (문자열이 페이지 경계 넘어설 때 추가 페이지 할당)
	// char *cmd_line_copy = palloc_get_multiple(0, (strlen(cmd_line) + 1 + (PGSIZE-1))/PGSIZE);
	char *cmd_line_copy = palloc_get_multiple(PAL_ZERO, (strlen(cmd_line) + 1 + (PGSIZE-1))/PGSIZE);
	// char *cmd_line_copy = palloc_get_page (0);
	if (cmd_line_copy == NULL) {
		return TID_ERROR;
	}
	strlcpy (cmd_line_copy, cmd_line, strlen (cmd_line) + 1);

	// change current process to the executable whose name is given in cmd_line
	tid_t exec_status = process_exec (cmd_line_copy);
	if (exec_status == TID_ERROR) {
		exit_handler (-1);
	}
	
	thread_current()->exit_status = exec_status;
	return exec_status;
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
wait_handler (int pid) {
	return process_wait ((tid_t) pid);
}

/**
 * Creates a new file called file initially initial_size bytes in size. Returns true if successful, false otherwise.
 * Creating a new file does not open it: opening the new file is a separate operation which would require a open system call.
 */
bool
create_handler (const char *file, unsigned initial_size) {
	validate_string_range (file);

	if (file == NULL) { // file이 NULL이면 종료
		exit_handler (-1);
	}
	return filesys_create(file, initial_size);
}

/**
 * Deletes the file called file. Returns true if successful, false otherwise.
 * A file may be removed regardless of whether it is open or closed, and removing an open file does not close it. See Removing an Open File in FAQ for details.
 */
bool
remove_handler (const char *file) {
	validate_string_range (file);

	if (file == NULL) { // file이 NULL이면 종료
		return false;
	}
	return filesys_remove(file);
}

/**
 * Opens the file called file. Returns a nonnegative integer handle called a "file descriptor" (fd), or -1 if the file could not be opened.
 * File descriptors numbered 0 and 1 are reserved for the console: fd 0 (STDIN_FILENO) is standard input, fd 1 (STDOUT_FILENO) is standard output.
 * The open system call will never return either of these file descriptors, which are valid as system call arguments only as explicitly described below. Each process has an independent set of file descriptors. File descriptors are inherited by child processes. When a single file is opened more than once, whether by a single process or different processes, each open returns a new file descriptor.
 * Different file descriptors for a single file are closed independently in separate calls to close and they do not share a file position. You should follow the linux scheme, which returns integer starting from zero, to do the extra.
 */
int
open_handler (const char *file_name) {
	validate_string_range (file_name);

	// open file from file system
	struct file *file = filesys_open (file_name);

	// if file is not found, return -1
	if (file == NULL) {
		return -1;
	}

	// add file to file descriptor table of the current thread
	struct thread *curr_thread = thread_current ();
	int fd = add_file_to_fd_table (file);
	return fd;
}

/**
 * Returns the size, in bytes, of the file open as fd.
 */
int
filesize_handler (int fd) {
	struct file *file = get_file_from_fd_table (fd);
	if (file == NULL) {
		return -1; // file descriptor not found or file is not open
	}

	return file_length (file);
}

/**
 * Reads size bytes from the file open as fd into buffer. Returns the number of bytes actually read (0 at end of file), or -1 if the file could not be read (due to a condition other than end of file).
 * fd 0 reads from the keyboard using input_getc().
 */
int
read_handler (int fd, void *buffer, unsigned size) {
	validate_address_range (buffer, size);

	unsigned int byte_counter = 0;

	if (fd == 0) {
		// 해당 과정이 I/O device와의 interaction이기에 syscall_lock
		lock_acquire (&syscall_lock);
		// size만큼 buffer에 입력받는 과정을 반복
		for (unsigned i = 0; i < size; i++) {
			// keyboard에서 입력받은 key(1 byte->char형)를 받아와서 buffer에 저장
			// buffer는 사용자가 입력한 key를 저장하는 공간으로 array.
			((char *) buffer)[i] = input_getc ();
			//1번 반복하면서 byte_counter 크기를 키워준다.
			byte_counter++;
		}
		lock_release (&syscall_lock);
		return byte_counter;
	}

	else if (fd < 0 || fd == NULL || fd == 1) {
		exit_handler(-1);
	}

	// find file from file descriptor table
	struct file *file = get_file_from_fd_table (fd);
	if (file == NULL) {
		exit_handler(-1);
		return -1; // file descriptor not found or file is not open
	}

	// file_read -> file_lock
	lock_acquire (&file_lock);
	byte_counter = file_read (file, buffer, size);
	lock_release (&file_lock);
	return byte_counter;
}

/**
 * Writes size bytes from buffer to the open file fd. Returns the number of bytes actually written, which may be less than size if some bytes could not be written.
 * Writing past end-of-file would normally extend the file, but file growth is not implemented by the basic file system. The expected behavior is to write as many bytes as possible up to end-of-file and return the actual number written, or 0 if no bytes could be written at all.
 * fd 1 writes to the console. Your code to write to the console should write all of buffer in one call to putbuf(), at least as long as size is not bigger than a few hundred bytes (It is reasonable to break up larger buffers). Otherwise, lines of text output by different processes may end up interleaved on the console, confusing both human readers and our grading scripts.
 */
int
write_handler (int fd, const void *buffer, unsigned size) {
	validate_address_range (buffer, size);

	// write to console
	if (fd == 1) {
		// console에 출력(I/O)하는 과정이기에 syscall_lock
		lock_acquire (&syscall_lock);
		putbuf (buffer, size);
		lock_release (&syscall_lock);
		return size;
	}
	else if (fd == 0){
		// fd == 0은 read. 종료시킨다.
		exit_handler(-1);
		// int형 반환을 위해 -1 반환
		return -1;
	}

	struct file *file = get_file_from_fd_table (fd);

	if (file != NULL) {
		lock_acquire (&file_lock);
		int bytes_written = file_write(file, buffer, size);
		lock_release (&file_lock);
		return bytes_written;
	}
	return -1; // file descriptor not found or file is not open
}

/**
 * Changes the next byte to be read or written in open file fd to position, expressed in bytes from the beginning of the file (Thus, a position of 0 is the file's start). A seek past the current end of a file is not an error. A later read obtains 0 bytes, indicating end of file. A later write extends the file, filling any unwritten gap with zeros. (However, in Pintos files have a fixed length until project 4 is complete, so writes past end of file will return an error.) These semantics are implemented in the file system and do not require any special effort in system call implementation.
 */
void
seek_handler (int fd, unsigned position) {
	struct file *file = get_file_from_fd_table (fd);
	if (file == NULL) {
		return; // file descriptor not found or file is not open
	}

	file_seek (file, position);
}

/**
 * Returns the position of the next byte to be read or written in open file fd, expressed in bytes from the beginning of the file.
 */
unsigned
tell_handler (int fd) {
	struct file *file = get_file_from_fd_table (fd);
	if (file == NULL) {
		return -1; // file descriptor not found or file is not open
	}

	return (unsigned)file_tell(file);
}

/**
 * Closes file descriptor fd. Exiting or terminating a process implicitly closes all its open file descriptors, as if by calling this function for each one.
 */
void
close_handler (int fd) {
	bool file_close_status = remove_file_from_fd_table (fd);
	if (!file_close_status) {
		exit_handler (-1);
	}
}

/**
 * Returns the file associated with the file descriptor fd from the file descriptor table of the current thread.
 */
struct file *
get_file_from_fd_table (int fd) {
	struct list *fdt = thread_current ()->fd_table;

	for (struct list_elem *e = list_begin (fdt); e != list_end (fdt); e = list_next (e)) {
		struct fd_elem *fd_elem = list_entry (e, struct fd_elem, elem);
		if (fd_elem->fd == fd) {
			return fd_elem->file;
		}
	}

	return NULL;
}

int
add_file_to_fd_table (struct file *file) {
	struct thread *curr_thread = thread_current ();
	struct list *fdt = curr_thread->fd_table;

	struct fd_elem *fd_elem = malloc (sizeof (struct fd_elem));
	if (fd_elem == NULL) {
		return -1;
	}

	lock_acquire (&file_lock);
	fd_elem->fd = next_fd;
	fd_elem->file = file;
	next_fd++;
	lock_release (&file_lock);

	list_push_back (fdt, &fd_elem->elem);

	return fd_elem->fd;
}

bool
remove_file_from_fd_table (int fd) {
	struct list *fdt = thread_current ()->fd_table;
	
	for (struct list_elem *e = list_begin (fdt); e != list_end (fdt); e = list_next (e)) {
		struct fd_elem *fd_elem = list_entry (e, struct fd_elem, elem);
		
		if (fd_elem->fd == fd) {
			list_remove (e);

			lock_acquire (&file_lock);
			file_close (fd_elem->file);
			lock_release (&file_lock);

			free (fd_elem);
			return true;
		}
	}
	return false;
}

void
validate_address_range (const void *addr, unsigned size) {
    unsigned i;
    uint64_t *pte;

		if (addr == NULL) {
			exit_handler(-1);
		}

		// define start_addr round down addr
		const void *start_addr = pg_round_down(addr);
    for (i = 0; i < size; i += PGSIZE) {
		const void *page_addr = pg_round_down(start_addr + i);
		if(!is_user_vaddr(page_addr)) {
			exit_handler(-1);
		}
        pte = pml4e_walk(thread_current()->pml4, (uint64_t) page_addr, 0);
				if (pte == NULL || !is_kernel_vaddr (pte)) {
						exit_handler(-1);
				}
    }
}

void
validate_string_range (const char *addr) {
		uint64_t *pte;

		if (addr == NULL) {
			exit_handler(-1);
		}

		// define start_addr round down addr
		const void *start_addr = pg_round_down(addr);
		for (int i = 0; ; i++) {
				pte = pml4e_walk(thread_current()->pml4, (uint64_t) pg_round_down(start_addr + i), 0);
				if (pte == NULL || !is_kernel_vaddr (pte)) {
						exit_handler(-1);
				}
				if (*(char *)(start_addr + i) == '\0') {
					break;
				}
		}
}