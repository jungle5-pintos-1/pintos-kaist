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
#include "include/lib/stdio.h"
#include "include/lib/string.h"
#include "userprog/process.h"
#include "threads/palloc.h"

void syscall_entry(void);
void syscall_handler(struct intr_frame *);
void check_address(void *addr);
void halt(void);
void exit(int status);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int open(const char *file_name);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);
int exec(const char *cmd_line);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081					/* Segment selector msr */
#define MSR_LSTAR 0xc0000082				/* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

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

	lock_init(&filesys_lock); // 파일 시스템 코드가 실행되는 동안에는 한 번에 하나의 프로세스만 실행되도록 동기화를 해야한다.
}

/* The main system call interface */
void syscall_handler(struct intr_frame *f UNUSED)
{
	// 일단 유저 스택에 저장되어 있는 시스템 콜 넘버를 가져온다
	int syscall_n = f->R.rax; // rax : 시스템 콜 넘버
	/**
	 * 인자 들어오는 순서:
	 * 1번째 인자 : %rdi
	 * 2번째 인자 : %rsi
	 * 3번째 인자 : %rdx
	 * 4번째 인자 : %r10
	 * 5번째 인자 : %r8
	 * 6번째 인자 : %r9
	 */
	switch (syscall_n)
	{
	case SYS_HALT:
		halt();
		break;
	case SYS_EXIT:
		exit(f->R.rdi);
		break;
	// case SYS_FORK:
	// 	f->R.rax =fork(f->R.rdi);
	// 	break;
	case SYS_EXEC:
		f->R.rax = exec(f->R.rdi);
		break;
	// case SYS_WAIT:
	// 	f->R.rax =wait(f->R.rdi);
	// 	break;
	case SYS_CREATE:
		f->R.rax = create(f->R.rdi, f->R.rsi);
		// printf("create: %d\n", f->R.rax);
		break;
	case SYS_REMOVE:
		f->R.rax = remove(f->R.rdi);
		break;
	case SYS_OPEN:
		f->R.rax = open(f->R.rdi);
		// printf("open: %d\n", f->R.rax);
		break;
	case SYS_FILESIZE:
		f->R.rax = filesize(f->R.rdi);
		break;
	case SYS_READ:
		f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
		// printf("read: %d\n", f->R.rax);
		break;
	case SYS_WRITE:
		f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		// printf("write: %d\n", f->R.rax);
		break;
	case SYS_SEEK:
		seek(f->R.rdi, f->R.rsi);
		break;
	case SYS_TELL:
		f->R.rax = tell(f->R.rdi);
		break;
	case SYS_CLOSE:
		close(f->R.rdi);
		break;
	default:
		printf("Wrong syscall_n : %d\n", syscall_n);
		thread_exit();
	}
	// printf("system call!\n");
	// thread_exit();
}

// User memory access
// 접근하는 메모리 주소가 유저 영역인지 커널 영역인지 체크
void check_address(void *addr)
{
	struct thread *t = thread_current();
	// 해당 주소값이 유저 가상 주소(user_vaddr)에 해당하는지?
	// pml4_get_page()는 유저 가상 주소와 대응하는 물리 주소를 확인하므로 NULL인지 확인
	// (페이지로 할당되지 않은 영역일 수도 있다)
	if (addr == NULL || !is_user_vaddr(addr))
	{
		exit(-1); // 유저 영역이 아니면 종료
	}
	if (pml4_get_page(t->pml4, addr) == NULL)
	{
		exit(-1);
	}
}

/* 호출시 pintos 종료 */
void halt(void)
{
	power_off();
}

/* 현재 프로세스를 종료 */
void exit(int status)
{
	struct thread *t = thread_current();
	t->exit_status = status;
	printf("%s: exit(%d)\n", t->name, t->exit_status); // 정상적으로 종료됐다면 status는 0
	thread_exit();
}

/* 파일을 생성하는 함수 */
bool create(const char *file, unsigned initial_size)
{
	check_address(file);
	// printf("Attempting to create file with name '%s' and size %u\n", file, initial_size);
	if (filesys_create(file, initial_size))
	{
		// printf("filesys_create returned %d\n", filesys_create(file, initial_size));
		return true;
	}
	else
	{
		// printf("filesys_create returned %d\n", filesys_create(file, initial_size));
		return false;
	}
}

/* 파일을 제거하는 함수 */
// 파일을 제거하더라도 그 이전에 파일을 오픈했다면
// 해당 오픈 파일은 close 되지 않고 그대로 켜진 상태로 남아있는다.
bool remove(const char *file)
{
	check_address(file);
	if (filesys_remove(file))
	{
		return true;
	}
	else
	{
		return false;
	}
}

/* 파일을 열 때 사용 */
int open(const char *file)
{
	check_address(file); // 유효한 지 확인
	lock_acquire(&filesys_lock);
	struct file *file_obj = filesys_open(file); // 열려고 하는 파일 객체정보 받기
	// printf("%d\n", 1);
	if (file_obj == NULL) // 생성 됐는지 확인
	{
		// printf("%d\n", 2);
		lock_release(&filesys_lock);
		return -1;
	}
	int fd = process_add_file(file_obj); // 만들어진 파일을 fdt 테이블에 추가
	// printf("open fd : %d\n", fd);
	if (fd == -1) // 열수 없으면 -1 리턴
	{
		// printf("%d\n", 2);
		file_close(file_obj); // 파일을 닫는다
	}
	lock_release(&filesys_lock);
	// printf("%d\n", 3);
	return fd;
}

/* 파일 사이즈 반환하는 함수 */
int filesize(int fd)
{
	struct file *file_obj = process_get_file(fd); // 파일 디스크립터 이용하여 파일 객체 검색
	if (file_obj == NULL)
	{
		return -1; // 존재하지 않으면 -1 리턴
	}
	return file_length(file_obj); // 파일 길이 리턴
}

/* 해당 파일로 부터 값을 읽어 버퍼에 넣는 함수 */
int read(int fd, void *buffer, unsigned size)
{
	// 유효한 주소인지 체크
	check_address(buffer);
	check_address(buffer + size - 1); // 버퍼 끝 주소도 유저 영역 내에 있는 지 확인
	unsigned char *buf = buffer;
	int read_bytes = 0;

	lock_acquire(&filesys_lock);
	if (fd == STDIN_FILENO) // STDIN
	{
		for (int i = 0; i < size; i++)
		{
			buf[i] = input_getc(); // 키보드 입력을 한 바이트 씩 버퍼에 저장
			read_bytes++;					 // 저장한 크기 키우기
		}
	}
	else if (fd == STDOUT_FILENO) // STDOUT
	{
		read_bytes = -1; // -1 이면 예외 처리
	}
	else
	{
		struct file *file_obj = process_get_file(fd);
		if (file_obj == NULL)
		{
			read_bytes = -1;
		}
		else
		{
			read_bytes = file_read(file_obj, buffer, size); // 파일의 데이터 크기만큼 저장
		}
	}
	lock_release(&filesys_lock);
	return read_bytes; // 읽은 바이트 수 리턴
}

/* 열린 파일의 데이터를 기록하는 함수 */
int write(int fd, void *buffer, unsigned size)
{
	check_address(buffer);
	int write_bytes = 0;

	lock_acquire(&filesys_lock);
	if (fd == STDOUT_FILENO)
	{
		putbuf(buffer, size);
		write_bytes = size;
	}
	else if (fd == STDIN_FILENO)
	{
		write_bytes = -1;
	}
	else
	{
		struct file *file_obj = process_get_file(fd);
		if (file_obj == NULL)
		{
			write_bytes = -1;
		}
		else
		{
			write_bytes = file_write(file_obj, buffer, size);
		}
	}
	lock_release(&filesys_lock);
	return write_bytes;
}

/* 파일 내에서 다음에 읽거나 쓸 바이트 위치를 변경하는 함수 */
void seek(int fd, unsigned position)
{
	struct file *file_obj = process_get_file(fd);
	if (file_obj == NULL)
	{
		return;
	}
	file_seek(file_obj, position);
}

/* 파일 내에서 다음에 읽거나 쓸 위치를 반환하는 함수 */
unsigned tell(int fd)
{
	struct file *file_obj = process_get_file(fd);
	if (file_obj == NULL)
	{
		return;
	}
	return file_tell(file_obj);
}

/* 파일 디스크립터를 닫는다 */
void close(int fd)
{
	struct file *file_obj = process_get_file(fd);
	// printf("close fd : %d\n", fd);
	if (file_obj == NULL)
	{
		return;
	}
	file_close(file_obj);
	process_close_file(fd);
}

/* 현재 프로세스를 cmd_line에 주어진 실행 파일로 변경하고, 필요한 인수를 전달 */
int exec(const char *cmd_line)
{
	// process_create_initd 함수와 유사
	// 새 스레드를 생성하는 것은 fork에서 수행

	check_address(cmd_line);

	// process_exec 함수 안에서 filename을 변경해야 하므로
	// 커널 메모리 공간에 cmd_line의 복사본을 만든다.
	// caller 함수와 load() 사이 race condition 방지
	char *cmd_line_copy;
	cmd_line_copy = palloc_get_page(0);
	if (cmd_line_copy == NULL)
	{
		exit(-1);
	}
	strlcpy(cmd_line_copy, cmd_line, PGSIZE);

	if (process_exec(cmd_line_copy) == -1)
	{
		exit(-1);
	}
}