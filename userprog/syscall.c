#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
/* NOTE: [2.2] 구현에 필요한 라이브러리 include */
#include "threads/init.h"
#include "filesys/filesys.h"
#include "lib/string.h"
#include "lib/syscall-nr.h"
#include "threads/malloc.h"
#include "lib/user/syscall.h"
#include "userprog/process.h"
#include "devices/input.h"
#include "threads/palloc.h"

void syscall_entry(void);
void syscall_handler(struct intr_frame *);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081			/* Segment selector msr */
#define MSR_LSTAR 0xc0000082		/* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

/* NOTE: [2.2] define */
#define USER_AREA_STAR 0x8048000
#define USER_AREA_END 0xc0000000

/* process */
void halt(void);
void exit(int status);
pid_t exec(const char *cmd_line);
pid_t sys_fork(const char *thread_name, struct intr_frame *f);
int wait(pid_t pid);
int open(const char *file_name);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);

/* file */
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);

void check_address(void *addr);

void syscall_init(void)
{
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 |
							((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			  FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

	/* NOTE: [2.4] filesys_lock 초기화 */
	lock_init(&filesys_lock);
}

/* The main system call interface */
void syscall_handler(struct intr_frame *f)
{
	// NOTE: [2.X] Your implementation goes here.
	/* TODO: [2.5] fork 추가 */
	uint64_t syscall_num = f->R.rax;

	switch (syscall_num)
	{
	case SYS_HALT: // 0
		halt();
		break;
	case SYS_EXIT: // 1
		exit(f->R.rdi);
		break;
	case SYS_FORK: // 2
		f->R.rax = sys_fork(f->R.rdi, f);
		break;
	case SYS_EXEC: // 3
		f->R.rax = exec(f->R.rdi);
		break;
	case SYS_WAIT: // 4
		f->R.rax = wait(f->R.rdi);
		break;
	case SYS_CREATE: // 5
		f->R.rax = create(f->R.rdi, f->R.rsi);
		break;
	case SYS_REMOVE: // 6
		f->R.rax = remove(f->R.rdi), sizeof(bool);
		break;
	case SYS_OPEN: // 7
		f->R.rax = open(f->R.rdi);
		break;
	case SYS_FILESIZE: // 8
		f->R.rax = filesize(f->R.rdi);
		break;
	case SYS_READ: // 9
		f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_WRITE: // 10
		f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_SEEK: // 11
		seek(f->R.rdi, f->R.rsi);
		break;
	case SYS_TELL: // 12
		f->R.rax = tell(f->R.rdi);
		break;
	case SYS_CLOSE: // 13
		close(f->R.rdi);
		break;
	default:
		thread_exit();
		break;
	}
}

/* ---------- SYSCALL ---------- */
/* NOTE: [2.2] pintos를 종료시키는 시스템 콜 */
void halt(void)
{
	/* power_off()를 사용하여 pintos 종료 */
	power_off();
}

/* NOTE: [2.2] 현재 프로세스를 종료시키는 시스템 콜 */
void exit(int status)
{
	/* 실행중인 스레드 구조체를 가져옴 */
	struct thread *curr = thread_current();
	/* NOTE: [2.3] 프로세스 디스크립터에 exit status 저장 */
	curr->exit_status = status;

	/* 프로세스 종료 메시지 출력, 출력 양식: “프로세스이름 : exit(종료상태 )” */
	printf("%s: exit(%d)\n", curr->name, status);
	/* 스레드 종료 */
	thread_exit();
}

/* NOTE: [2.5] fork() 시스템 콜 구현 */
pid_t sys_fork(const char *thread_name, struct intr_frame *f)
{
	check_address(thread_name);
	return process_fork(thread_name, f);
}

/* NOTE: [2.3] exec() 시스템 콜 구현 */
pid_t exec(const char *cmd_line)
{
	check_address(cmd_line);

	char *cmd_line_cpy = palloc_get_page(0);
	if (cmd_line_cpy == NULL)
		exit(-1);
	strlcpy(cmd_line_cpy, cmd_line, PGSIZE);

	if (process_exec(cmd_line_cpy) == -1)
	{
		palloc_free_page(cmd_line_cpy);
		exit(-1);
	}
}

/* NOTE: [2.3] wait() 시스템 콜 구현 */
int wait(pid_t pid)
{
	/* 자식 프로세스가 종료 될 때까지 대기*/
	/* process_wait() 사용 */
	return process_wait(pid);
}

/* NOTE: [2.2] 파일을 생성하는 시스템 콜*/
bool create(const char *file, unsigned initial_size)
{
	check_address(file);

	bool success;
	lock_acquire(&filesys_lock);
	/* 파일 이름과 크기에 해당하는 파일 생성*/
	success = filesys_create(file, initial_size);
	lock_release(&filesys_lock);
	/* 파일 생성 성공 시 true 반환, 실패 시 false 반환 */
	return success;
}

/* NOTE: [2.2] 파일을 삭제하는 시스템 콜 */
bool remove(const char *file)
{
	check_address(file);

	bool success;
	lock_acquire(&filesys_lock);
	/* 파일 이름에 해당하는 파일을 제거*/
	success = filesys_remove(file);
	lock_release(&filesys_lock);
	/* 파일 제거 성공 시 true 반환, 실패 시 false 반환 */
	return success;
}

/* NOTE: [2.4] open() 시스템 콜 구현 */
int open(const char *file_name)
{
	check_address(file_name);
	lock_acquire(&filesys_lock);
	/* 파일을 open */
	int fd = -1;
	struct file *file = filesys_open(file_name);

	/* 해당 파일 객체에 파일 디스크립터 부여*/
	/* 파일 디스크립터 리턴*/
	if (file != NULL)
		fd = process_add_file(file);
	if (fd == -1)
		file_close(file);
	lock_release(&filesys_lock);

	/* 해당 파일이 존재하지 않으면 -1 리턴 */
	return fd;
}

/* NOTE: [2.4] filesize() 시스템 콜 구현 */
int filesize(int fd)
{
	lock_acquire(&filesys_lock);
	/* 파일 디스크립터를 이용하여 파일 객체 검색 */
	struct file *file = process_get_file(fd);
	int size = -1;
	/* 해당 파일의 길이를 리턴 */
	if (file != NULL)
		size = file_length(file);

	lock_release(&filesys_lock);
	/* 해당 파일이 존재하지 않으면 -1 리턴 */
	return size;
}

/* NOTE: [2.4] read() 시스템 콜 구현 */
int read(int fd, void *buffer, unsigned size)
{
	check_address(buffer);

	/* 파일에 동시 접근이 일어날 수 있으므로 Lock 사용 */
	lock_acquire(&filesys_lock);
	/* 파일 디스크립터를 이용하여 파일 객체 검색 */
	struct file *file = process_get_file(fd);
	/* 파일 디스크립터가 0일 경우 키보드에 입력을 버퍼에 저장 후 버퍼의 저장한 크기를 리턴 (input_getc() 이용) */
	if (fd == 0)
	{
		uint8_t user_input = input_getc();
		memcpy(buffer, &user_input, sizeof(user_input));
		lock_release(&filesys_lock);
		return sizeof(user_input);
	}
	/* 파일 디스크립터가 0이 아닐 경우 파일의 데이터를 크기만큼 저장 후 읽은 바이트 수를 리턴 */
	if (fd >= 2 && file)
	{
		int bytes = file_read(file, buffer, size);
		lock_release(&filesys_lock);
		return bytes;
	}
	lock_release(&filesys_lock);
	return -1;
}

/* NOTE: [2.4] write() 시스템 콜 구현 */
int write(int fd, const void *buffer, unsigned size)
{
	check_address(buffer);

	/* 파일에 동시 접근이 일어날 수 있으므로 Lock 사용 */
	lock_acquire(&filesys_lock);
	/* 파일 디스크립터를 이용하여 파일 객체 검색 */
	struct file *file = process_get_file(fd);
	/* 파일 디스크립터가 1일 경우 버퍼에 저장된 값을 화면에 출력 후 버퍼의 크기 리턴 (putbuf() 이용) */
	if (fd == 1)
	{
		putbuf(buffer, size);
		lock_release(&filesys_lock);
		return sizeof(buffer);
	}
	/* 파일 디스크립터가 1이 아닐 경우 버퍼에 저장된 데이터를 크기만큼 파일에 기록 후 기록한 바이트 수를 리턴 */
	if (fd >= 2 && file)
	{
		int bytes = file_write(file, buffer, size);
		lock_release(&filesys_lock);
		return bytes;
	}
	lock_release(&filesys_lock);
	return -1;
}

/* NOTE: [2.4] seek() 시스템 콜 구현 */
void seek(int fd, unsigned position)
{
	lock_acquire(&filesys_lock);
	/* 파일 디스크립터를 이용하여 파일 객체 검색 */
	struct file *file = process_get_file(fd);
	/* 해당 열린 파일의 위치(offset)를 position만큼 이동 */
	if (file)
		file_seek(file, position);
	lock_release(&filesys_lock);
}

/* NOTE: [2.4] tell() 시스템 콜 구현 */
unsigned tell(int fd)
{
	lock_acquire(&filesys_lock);
	/* 파일 디스크립터를 이용하여 파일 객체 검색 */
	struct file *file = process_get_file(fd);
	unsigned position = -1;
	/* 해당 열린 파일의 위치를 반환 */
	if (file)
		position = file_tell(file);
	lock_release(&filesys_lock);
	return position;
}

/* NOTE: [2.4] close() 시스템 콜 구현 */
void close(int fd)
{
	lock_acquire(&filesys_lock);
	/* 해당 파일 디스크립터에 해당하는 파일을 닫음 */
	process_close_file(fd);
	lock_release(&filesys_lock);
}

/* ---------- UTIL ---------- */
/* NOTE: [2.2] 추가 함수 - 주소 값이 유저 영역에서 사용하는 주소 값인지 확인하는 함수 */
void check_address(void *addr)
{
	if (addr == NULL)
		exit(-1);
	if (!is_user_vaddr(addr))
		exit(-1);
}
