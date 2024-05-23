#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
/* NOTE: [Part2] 구현에 필요한 라이브러리 include */
#include "include/threads/init.h"
#include "filesys/filesys.h"
#include "lib/string.h"
#include "lib/syscall-nr.h"
#include "include/threads/malloc.h"

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

/* NOTE: [Part2] define */
#define USER_AREA_STAR 0x8048000
#define USER_AREA_END 0xc0000000

void halt(void);
void exit(int status);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
void check_address(void *addr);
void get_argument(void *rsp, int *arg, int count);

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
}

/* The main system call interface */
void syscall_handler(struct intr_frame *f)
{
	// NOTE: [Part2] Your implementation goes here.
	/* 유저 스택에 저장되어 있는 시스템 콜 넘버를 이용해 시스템 콜 핸들러 구현*/
	/* 스택 포인터가 유저 영역인지 확인 */
	/* 저장된 인자 값이 포인터일 경우 유저 영역의 주소인지 확인 */
	/* 0 : halt */
	/* 1 : exit */
	/* . . . */

	void *rsp = f->rsp;
	check_address(rsp);
	int syscall_num = *(int *)rsp;
	rsp += sizeof(int *);

	int *argv = NULL;
	bool result;

	switch (syscall_num)
	{
	case SYS_HALT:
		halt();
		break;
	case SYS_EXIT:
		argv = realloc(argv, sizeof(int));
		get_argument(&rsp, argv, 1);
		exit(argv[0]);
		break;
	case SYS_CREATE:
		argv = realloc(argv, sizeof(int) * 2);
		get_argument(&rsp, argv, 2);
		result = create(argv[0], argv[1]);
		memcpy(f->R.rax, &result, sizeof(bool));
		break;
	case SYS_REMOVE:
		argv = realloc(argv, sizeof(int));
		get_argument(&rsp, argv, 1);
		result = remove(argv[0]);
		memcpy(f->R.rax, &result, sizeof(bool));
		break;
	default:
		thread_exit();
		break;
	}
}

/* NOTE: [Part2] pintos를 종료시키는 시스템 콜 */
void halt(void)
{
	/* power_off()를 사용하여 pintos 종료 */
	power_off();
}

/* NOTE: [Part2] 현재 프로세스를 종료시키는 시스템 콜 */
void exit(int status)
{
	/* 실행중인 스레드 구조체를 가져옴 */
	struct thread *curr = thread_current();
	/* 프로세스 종료 메시지 출력,
	출력 양식: “프로세스이름 : exit(종료상태 )” */
	printf("%s : exit(%d)\n", curr->name, status);
	/* 스레드 종료 */
	thread_exit();
}

/* NOTE: [Part2] 파일을 생성하는 시스템 콜*/
bool create(const char *file, unsigned initial_size)
{
	bool success;
	/* 파일 이름과 크기에 해당하는 파일 생성*/
	success = filesys_create(file, initial_size);
	/* 파일 생성 성공 시 true 반환, 실패 시 false 반환 */
	return success;
}

/* NOTE: [Part2] 파일을 삭제하는 시스템 콜 */
bool remove(const char *file)
{
	bool success;
	/* 파일 이름에 해당하는 파일을 제거*/
	success = filesys_remove(file);
	/* 파일 제거 성공 시 true 반환, 실패 시 false 반환 */
	return success;
}

/* NOTE: [Part2] 추가 함수 - 주소 값이 유저 영역에서 사용하는 주소 값인지 확인하는 함수 */
void check_address(void *addr)
{
	/* 포인터가 가리키는 주소가 유저영역의 주소인지 확인*/
	if (addr < USER_AREA_STAR || addr > USER_AREA_END)
	{
		/* 잘못된 접근일 경우 프로세스 종료 */
		exit(-1);
	}
}

/* NOTE: [Part2] 추가 함수 - 유저 스택에 있는 인자들을 커널에 저장하는 함수 */
void get_argument(void *rsp, int *arg, int count)
{
	/* 유저 스택에 저장된 인자값들을 커널로 저장*/
	for (int i = 0; i < count; i++)
	{
		/* 인자가 저장된 위치가 유저영역인지 확인 */
		check_address(rsp);

		/* 인자를 커널로 복사 */
		memcpy(&arg[i], rsp, sizeof(int *));
		rsp += sizeof(int *);
	}
}