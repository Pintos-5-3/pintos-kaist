#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
/* NOTE: [2.1] 실행에 필요한 헤더 파일 include*/
#include "lib/string.h"
#include "lib/stdio.h"
#include "threads/loader.h"
#include "filesys/file.h"
#include "threads/malloc.h"
#include "userprog/syscall.h"

#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup(void);
static bool load(const char *file_name, struct intr_frame *if_);
static void initd(void *f_name);
static void __do_fork(void *);
static void argument_stack(char **parse, int count, void **rsp);

/* General process initializer for initd and other process. */
static void process_init(void)
{
	struct thread *current = thread_current();
}

/**
 * @brief "initd"라고 불리는 첫 번째 유저 레벨 프로그램을 시작하는 함수 /
 * 새로운 스레드는 process_create_initd()가 반환되기 전에 스케줄링되거나 종료될 수 있습니다.
 * 이 함수는 "한 번만 호출"되어야 합니다.
 *
 * @param file_name 로드할 프로그램의 파일 이름
 * @return tid_t 생성된 스레드의 ID 또는 오류 시 TID_ERROR
 */
tid_t process_create_initd(const char *file_name) /* NOTE: process_excute() */
{
	char *fn_copy;
	tid_t tid;

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page(0);
	if (fn_copy == NULL)
		return TID_ERROR;

	strlcpy(fn_copy, file_name, PGSIZE);

	/* NOTE: [2.1] program_name 파싱해서 thread_create에 전달 */
	char *save_ptr;
	file_name = strtok_r(file_name, " ", &save_ptr);
	/* Create a new thread to execute FILE_NAME. */
	tid = thread_create(file_name, PRI_DEFAULT, initd, fn_copy);
	if (tid == TID_ERROR)
		palloc_free_page(fn_copy);
	return tid;
}

/* A thread function that launches first user process. */
static void
initd(void *f_name)
{
#ifdef VM
	supplemental_page_table_init(&thread_current()->spt);
#endif
	process_init();

	if (process_exec(f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED();
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
tid_t process_fork(const char *name, struct intr_frame *if_)
{
	struct thread *curr = thread_current();
	memcpy(&curr->parent_if, if_, sizeof(struct intr_frame));

	/* Clone current thread to new thread.*/
	tid_t tid = thread_create(name, PRI_DEFAULT, __do_fork, curr);
	if (tid == TID_ERROR)
		return TID_ERROR;
	struct thread *child = get_child_process(tid);
	sema_down(&child->load_sema);
	if (child->exit_status == TID_ERROR)
		return TID_ERROR;
	return tid;
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */

/* NOTE: [2.5] 전체 사용자 메모리 공간을 복사하는 함수에서 빠진 부분 구현 */
static bool duplicate_pte(uint64_t *pte, void *va, void *aux)
{
	struct thread *current = thread_current();
	struct thread *parent = (struct thread *)aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. NOTE: If the parent_page is kernel page, then return immediately. */
	if (is_kernel_vaddr(va))
		return true;

	/* 2. Resolve VA from the parent's page map level 4. */
	parent_page = pml4_get_page(parent->pml4, va);
	if (parent_page == NULL)
		return false;

	/* 3. NOTE: Allocate new PAL_USER page for the child and set result to NEWPAGE. */
	newpage = palloc_get_page(PAL_USER | PAL_ZERO);
	if (newpage == NULL)
		return false;

	/* 4. NOTE: Duplicate parent's page to the new page and check whether parent's page is writable or not (set WRITABLE according to the result). */
	memcpy(newpage, parent_page, PGSIZE);
	writable = is_writable(pte);

	/* 5. Add new page to child's page table at address VA with WRITABLE permission. */
	if (!pml4_set_page(current->pml4, va, newpage, writable))
	{
		/* 6. NOTE: if fail to insert page, do error handling. */
		palloc_free_page(newpage);
		return false;
	}
	return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
static void
__do_fork(void *aux)
{
	struct intr_frame if_;
	struct thread *parent = (struct thread *)aux;
	struct thread *current = thread_current();
	/* NOTE: somehow pass the parent_if. (i.e. process_fork()'s if_) */
	struct intr_frame *parent_if = &parent->parent_if;
	bool succ = true;

	/* 1. Read the cpu context to local stack. */
	memcpy(&if_, parent_if, sizeof(struct intr_frame));
	/* NOTE: [2.5] 자식 프로세스의 리턴값을 0으로 지정 */
	if_.R.rax = 0;

	/* 2. Duplicate PT */
	current->pml4 = pml4_create();
	if (current->pml4 == NULL)
		goto error;

	process_activate(current);
#ifdef VM
	supplemental_page_table_init(&current->spt);
	if (!supplemental_page_table_copy(&current->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each(parent->pml4, duplicate_pte, parent))
		goto error;
#endif
	/* NOTE: Your code goes here.
	 * NOTE: Hint) To duplicate the file object, use `file_duplicate`
	 * NOTE:       in include/filesys/file.h. Note that parent should not return
	 * NOTE:       from the fork() until this function successfully duplicates
	 * NOTE:       the resources of parent.*/

	for (int i = 0; i < FDT_MAX; i++)
	{
		struct file *file = parent->fdt[i];
		if (file == NULL)
			continue;
		if (file > 2)
			file = file_duplicate(file);
		current->fdt[i] = file;
	}
	sema_up(&current->load_sema);
	process_init();

	/* Finally, switch to the newly created process. */
	if (succ)
		do_iret(&if_);
error:
	sema_up(&current->load_sema);
	exit(TID_ERROR);
}

/**
 * @brief 현재 실행 컨텍스트를 f_name이 가리키는 프로세스로 전환하는 함수
 *
 * @param f_name 실행할 프로세스의 이름 (파싱 안 된 값)
 * @return int 실패 시 -1 반환
 */
int process_exec(void *f_name) /* NOTE: 강의의 start_process() */
{
	char *token, *saveptr;
	char *file_name = strtok_r(f_name, " ", &saveptr);
	bool success;

	char **parse = malloc(128 * sizeof(char *));
	strlcpy(parse, file_name, strlen(file_name) + 1);
	int count = 1;

	for (token = strtok_r(NULL, " ", &saveptr); token != NULL; token = strtok_r(NULL, " ", &saveptr))
	{
		strlcpy(parse + count * sizeof(char *), token, strlen(token) + 1);
		count++;
	}

	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	/* We first kill the current context */
	process_cleanup();

	// lock_acquire(&filesys_lock);
	/* And then load the binary */
	success = load(file_name, &_if);
	// lock_release(&filesys_lock);

	/* NOTE: [2.3] 메모리 적재 완료 시 부모 프로세스 다시 진행 (세마포어 이용) */
	// sema_up(&thread_current()->load_sema);

	/* If load failed, quit. */
	palloc_free_page(f_name);
	if (!success)
	{
		free(parse);
		return -1;
	}

	/* NOTE: [2.1] 스택에 인자 push 후 dump로 출력 */
	argument_stack(parse, count, &_if.rsp);
	free(parse);
	// hex_dump(_if.rsp, _if.rsp, USER_STACK - _if.rsp, true);
	_if.R.rsi = _if.rsp + sizeof(void (*)());
	_if.R.rdi = count;

	/* Start switched process. */
	do_iret(&_if);
	NOT_REACHED();
}

/**
 * @brief
 *
 * @param parse 프로그램 이름과 인자가 저장되어 있는 메모리 공간
 * @param count 인자의 개수
 * @param esp 스택 포인터를 가리키는 주소 값
 */
static void argument_stack(char **parse, int count, void **rsp)
{
	char *address[count + 1];
	memset(address, NULL, sizeof(address));

	int len;
	/* Argument */
	for (int i = count - 1; i > -1; i--)
	{
		len = strlen(parse + i * sizeof(char *)) + 1; // parse[i]
		*rsp = *rsp - len;
		memcpy(*rsp, parse + i * sizeof(char *), len);
		address[i] = *rsp;
	}

	/* word-align */
	uint8_t align = (uint8_t)(*rsp) % 8;
	if (align != 0)
	{
		*rsp = *rsp - align;
		memset(*rsp, 0, align);
	}

	/* Argument의 주소 */
	for (int i = count; i > -1; i--)
	{
		*rsp = *rsp - sizeof(char *);
		*(char **)(*rsp) = address[i];
	}

	/* fake addreass(0) */
	*rsp = *rsp - sizeof(void (*)());
	*(void (**)())(*rsp) = 0;
}

/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
int process_wait(tid_t child_tid)
{
	/* NOTE: [2.3] 자식 프로세스가 수행되고 종료될 때까지 부모 프로세스 대기 */
	struct thread *child;
	int exit_status;

	/* 자식 프로세스의 프로세스 디스크립터 검색*/
	child = get_child_process(child_tid);

	/* 예외 처리 발생 시 -1 리턴 */
	if (child == NULL)
		return -1;

	/* 자식프로세스가 종료될 때까지 부모 프로세스 대기(세마포어 이용) */
	sema_down(&child->wait_sema);

	/* 자식 프로세스 디스크립터 삭제*/
	exit_status = child->exit_status;
	list_remove(&child->c_elem);

	sema_up(&child->exit_sema);
	/* 자식 프로세스의 exit status 리턴*/
	return exit_status;
}

/* Exit the process. This function is called by thread_exit (). */
void process_exit(void)
{
	struct thread *curr = thread_current();

	/* NOTE: [2.5] run_file 닫아주기 */
	file_close(curr->run_file);
	curr->run_file = NULL;

	/* NOTE: [2.4] 모든 열린 파일 닫기 */
	for (int idx = 2; idx < FDT_MAX; idx++)
		file_close(process_get_file(idx));
	palloc_free_page(curr->fdt);
	process_cleanup();

	/* NOTE: [2.3] thread_exit 수정 */
	/* 부모 프로세스를 대기 상태에서 이탈시킴 (세마포어 이용) */
	sema_up(&thread_current()->wait_sema);
	sema_down(&thread_current()->exit_sema);
}

/**
 * @brief 현재 프로세스의 자원을 해제하는 함수 /
 * 현재 프로세스의 페이지 테이블을 파괴하고 커널 전용 페이지 테이블로 전환하는 작업을 수행합니다.
 */
static void
process_cleanup(void)
{
	struct thread *curr = thread_current();

#ifdef VM
	supplemental_page_table_kill(&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL)
	{
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		curr->pml4 = NULL;
		pml4_activate(NULL);
		pml4_destroy(pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void process_activate(struct thread *next)
{
	/* Activate thread's page tables. */
	pml4_activate(next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update(next);
}

/* ---------- 파일 디스크립터 ---------- */

/* NOTE: [2.4] 파일 디스크립터 생성 함수 구현 */
int process_add_file(struct file *f)
{
	struct thread *curr = thread_current();

	for (int idx = 2; idx < FDT_MAX; idx++)
	{
		if (curr->fdt[idx] == NULL)
		{
			curr->fdt[idx] = f;
			/* 파일 디스크립터 리턴 */
			return idx;
		}
	}
	return -1;
}

/* NOTE: [2.4] 파일 객체 검색 함수 구현 */
struct file *process_get_file(int fd)
{
	/* fd 범위 체크 */
	if (fd < 2 || fd >= FDT_MAX)
		return NULL;
	/* 파일 디스크립터에 해당하는 파일 객체를 리턴*/
	struct file *f = thread_current()->fdt[fd];
	return f;
}

/* NOTE: [2.4] 파일을 닫는 함수 구현 */
void process_close_file(int fd)
{
	/* fd 범위 체크 */
	if (fd < 2 || fd >= FDT_MAX)
		return;
	struct thread *curr = thread_current();
	struct file *file = process_get_file(fd);
	// lock_acquire(&filesys_lock);
	file_close(file);
	// lock_release(&filesys_lock);
	
	/* 파일 디스크립터 테이블 해당 엔트리 초기화*/
	curr->fdt[fd] = NULL;
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL 0			/* Ignore. */
#define PT_LOAD 1			/* Loadable segment. */
#define PT_DYNAMIC 2		/* Dynamic linking info. */
#define PT_INTERP 3			/* Name of dynamic loader. */
#define PT_NOTE 4			/* Auxiliary info. */
#define PT_SHLIB 5			/* Reserved. */
#define PT_PHDR 6			/* Program header table. */
#define PT_STACK 0x6474e551 /* Stack segment. */

#define PF_X 1 /* Executable. */
#define PF_W 2 /* Writable. */
#define PF_R 4 /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr
{
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR
{
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack(struct intr_frame *if_);
static bool validate_segment(const struct Phdr *, struct file *);
static bool load_segment(struct file *file, off_t ofs, uint8_t *upage,
						 uint32_t read_bytes, uint32_t zero_bytes,
						 bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
static bool
load(const char *file_name, struct intr_frame *if_)
{
	struct thread *t = thread_current();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	// hex_dump(&file_ofs, )
	/* Allocate and activate page directory. */
	t->pml4 = pml4_create();
	if (t->pml4 == NULL)
		goto done;
	process_activate(thread_current());

	/* Open executable file. */
	lock_acquire(&filesys_lock);
	file = filesys_open(file_name);
	if (file == NULL)
	{
		printf("load: %s: open failed\n", file_name);
		goto done;
	}

	/* Read and verify executable header. */
	if (file_read(file, &ehdr, sizeof ehdr) != sizeof ehdr || memcmp(ehdr.e_ident, "\177ELF\2\1\1", 7) || ehdr.e_type != 2 || ehdr.e_machine != 0x3E // amd64
		|| ehdr.e_version != 1 || ehdr.e_phentsize != sizeof(struct Phdr) || ehdr.e_phnum > 1024)
	{
		printf("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++)
	{
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length(file))
			goto done;
		file_seek(file, file_ofs);

		if (file_read(file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type)
		{
		case PT_NULL:
		case PT_NOTE:
		case PT_PHDR:
		case PT_STACK:
		default:
			/* Ignore this segment. */
			break;
		case PT_DYNAMIC:
		case PT_INTERP:
		case PT_SHLIB:
			goto done;
		case PT_LOAD:
			if (validate_segment(&phdr, file))
			{
				bool writable = (phdr.p_flags & PF_W) != 0;
				uint64_t file_page = phdr.p_offset & ~PGMASK;
				uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
				uint64_t page_offset = phdr.p_vaddr & PGMASK;
				uint32_t read_bytes, zero_bytes;
				if (phdr.p_filesz > 0)
				{
					/* Normal segment.
					 * Read initial part from disk and zero the rest. */
					read_bytes = page_offset + phdr.p_filesz;
					zero_bytes = (ROUND_UP(page_offset + phdr.p_memsz, PGSIZE) - read_bytes);
				}
				else
				{
					/* Entirely zero.
					 * Don't read anything from disk. */
					read_bytes = 0;
					zero_bytes = ROUND_UP(page_offset + phdr.p_memsz, PGSIZE);
				}
				if (!load_segment(file, file_page, (void *)mem_page,
								  read_bytes, zero_bytes, writable))
					goto done;
			}
			else
				goto done;
			break;
		}
	}
	/* NOTE: [2.5] 파일 open 시 file_deny_write() 호출 / thread 구조체에 실행 중인 파일 추가 */
	file_deny_write(file);
	t->run_file = file;

	/* Set up stack. */
	if (!setup_stack(if_))
		goto done;

	/* Start address. */
	if_->rip = ehdr.e_entry;

	success = true;

done:
	/* We arrive here whether the load is successful or not. */
	// file_close(file);
	lock_release(&filesys_lock);
	return success;
}

/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment(const struct Phdr *phdr, struct file *file)
{
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t)file_length(file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr((void *)phdr->p_vaddr))
		return false;
	if (!is_user_vaddr((void *)(phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page(void *upage, void *kpage, bool writable);

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */


/*
load_segment 
- 프로세스가 실행될 때 실행 파일을 현재 쓰레드로 로드하는 load 함수에서 호출됨
*/
static bool
load_segment(struct file *file, off_t ofs, uint8_t *upage,
			 uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
	ASSERT((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT(pg_ofs(upage) == 0);
	ASSERT(ofs % PGSIZE == 0);

	file_seek(file, ofs);
	while (read_bytes > 0 || zero_bytes > 0)
	{
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. 메모리 할당 */
		uint8_t *kpage = palloc_get_page(PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read(file, kpage, page_read_bytes) != (int)page_read_bytes)
		{
			palloc_free_page(kpage);
			return false;
		}
		memset(kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page(upage, kpage, writable))
		{
			printf("fail\n");
			palloc_free_page(kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK 
가상 메모리에 페이지 할당 -> 사용자 스택에 매핑하여 최소한의 스택 환경 구성
- 첫 스택 페이지는 Lazy load될 필요가 없다? 
*/
static bool
setup_stack(struct intr_frame *if_)
{
	uint8_t *kpage; //커널에서 사용할 페이지 포인터
	bool success = false;

	//사용자 모드에서 사용할 페이지 할당, 모든 바이트 0으로 초기화
	kpage = palloc_get_page(PAL_USER | PAL_ZERO); 

	if (kpage != NULL)
	{
		/*할당된 페이지를 사용자 스택의 top 주소로 매핑
		- 스택은 높은 주소에서 낮은 주소로 성장 
		(USER_STACK - 한 페이지의 크기) => 새로운 스택의 시작점으로 설정*/
		success = install_page(((uint8_t *)USER_STACK) - PGSIZE, kpage, true);

		if (success)
			if_->rsp = USER_STACK; //스택 포인터를 새로운 사용자 스택의 최상단 주소로 
		else
			palloc_free_page(kpage); //할당된 페이지 해제
	}
	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool
install_page(void *upage, void *kpage, bool writable)
{
	struct thread *t = thread_current();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page(t->pml4, upage) == NULL && pml4_set_page(t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */

/*
lazy_load_segment 
- page fault 발생 시 세그먼트를 메모리에 로드하는 함수 
- uninit_initialize에서 첫 페이지 폴트가 난 뒤에 lazy_load_segment 수행
- 함수가 호출되기 이전에 물리 프레임 매핑이 진행됨 - 물리 프레임에 내용을 Load하는 작업을 해야 함 
*/
bool
lazy_load_segment(struct page *page, void *aux)
{
	/* TODO: Load the segment from the file. 파일로부터 세그먼트를 메모리에 로드해야 함*/
	/* TODO: This called when the first page fault occurs on address VA. 
	특정 주소 VA 접근할 때 청므으로 페이지 폴트가 발생되면 실행된다.*/
	/* TODO: VA is available when calling this function. 
	함수가 호출될 떄 가상 주소 VA가 사용 가능하다. - VA는 이미 정의되어 있음. 함수 내에서 사용 가능하다.*/
	struct lazy_load_arg *lazy_load_arg = (struct lazy_load_arg *)aux;

	struct file *file = lazy_load_arg->file;
	off_t offset =  lazy_load_arg->ofs;
	size_t page_read_bytes = lazy_load_arg->read_bytes;
	size_t page_zero_bytes = lazy_load_arg->zero_bytes;
	//size_t page_zero_bytes = PGSIZE - page_read_bytes;

	// lock_acquire(&filesys_lock);
	//파일의 오프셋 설정 - 파일의 현재 위치 변경 
	file_seek(file, offset);

	//파일에서 데이터를 읽어 page frame에 로드 
	if (file_read(file, page->frame->kva, page_read_bytes) != (int)page_read_bytes) {
		//읽기에 실패한 경우 할당된 페이지 해제 
		palloc_free_page(page->frame->kva);
		// lock_release(&filesys_lock);
		// printf("file_read fail: read_byte %d\n", page_read_bytes);
		return false;
	}

	//페이지의 kva 주소에서 read_bytes만큼 떨어진 위치부터 나머지 바이트(zero_bytes)만큼을 0으로 채운다.
	memset(page->frame->kva + page_read_bytes, 0, page_zero_bytes);
	// lock_release(&filesys_lock);

	return true;
}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */

/* 파일로부터 데이터를 읽어와서 메모리에 로드하는 역할 
- read_bytes: 파일에서 읽어야 할 남은 바이트 수 
- zero_bytes: 메모리에서 0으로 초기화해야 할 남은 바이트 수 
- upage: 현재 메모리 페이지의 시작 주소,
- ofs: 파일에서 현재 읽기 시작할 offset*/
static bool
load_segment(struct file *file, off_t ofs, uint8_t *upage,
			 uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
	ASSERT((read_bytes + zero_bytes) % PGSIZE == 0); //읽을 바이트와 0으로 채울 바이트의 합이 페이지 크기의 배수인지 확인 
	ASSERT(pg_ofs(upage) == 0); //upage가 페이자 정렬되어 있는지 확인
	ASSERT(ofs % PGSIZE == 0); //파일 오프셋이 페이지 크기의 배수인지 확인

	//read_bytes, zero_bytes가 모두 0이 될 때까지 loop 실행
	while (read_bytes > 0 || zero_bytes > 0)
	{
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment.*/
		/*lazy_load_segment에 정보를 전달하기 위한 aux 설정*/
		struct lazy_load_arg *lazy_load_arg = (struct lazy_load_arg *)malloc(sizeof(struct lazy_load_arg));

		lazy_load_arg->file = file;
		lazy_load_arg->read_bytes = page_read_bytes; //읽어야 하는 바이트 수 
		lazy_load_arg->zero_bytes = page_zero_bytes; //read_bytes만큼 읽고 나서 0으로 채워야 하는 바이트 수
		lazy_load_arg->ofs = ofs; //offset - 페이지에서 읽기 시작할 위치 


		/*vm_alloc_page_with_initializer 
			- true : 페이지 할당, 초기화 성공 
			- false => 페이지 할당, 초기화 실패
			aux 인자에 lazy_load_arg를 전달*/
		if (!vm_alloc_page_with_initializer(VM_ANON, upage,
											writable, lazy_load_segment, lazy_load_arg))
			return false;

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE; //다음 페이지의 시작 주소로 업데이트
		ofs += page_read_bytes; //다음 읽기 위치 update

	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
static bool
setup_stack(struct intr_frame *if_)
{
	bool success = false;

	/* stack 아래로 성장 -> USER_STACK(스택의 시작점)에서 PGSIZE만큼 내린 지점에서 페이지를 생성함 */
	void *stack_bottom = (void *)(((uint8_t *)USER_STACK) - PGSIZE);
	

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */
	
	/* 
	첫 번째 스택 페이지는 lazy loading할 필요가 없음 
	=> page fault가 발생할 때까지 기다릴 필요가 없이 바로 물리 프레임 할당
	- stack_bottom에 스택을 매핑하고 해당 페이지를 즉시 할당
	- 성공했다면 rsp(스택 포인터)를 적절한 위치로 설정
	- 해당 페이지가 stack임을 표시하라 - VM_MARKER_0
	*/
	
	/*
	vm_alloc_page(type, upage, writable)함수는 vm_alloc_page_with_initializer에서 
	1, 2, 3번째 인자만 받는 함수 4, 5번째 함수는 NULL로 받는다. 
	*/
	if (vm_alloc_page(VM_ANON | VM_MARKER_0, stack_bottom, 1)) {
		
		success= vm_claim_page(stack_bottom); //물리 페이지 프레임 할당

		if (success) {
			if_->rsp = USER_STACK;
			thread_current()->stack_bottom = stack_bottom;
		}
	}
	return success;
}
#endif /* VM */
