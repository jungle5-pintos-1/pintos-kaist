#include "userprog/syscall.h"

#include <stdio.h>
#include <syscall-nr.h>

#include "filesys/filesys.h"
#include "intrinsic.h"
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/loader.h"
#include "threads/thread.h"
#include "userprog/gdt.h"
#include "user/syscall.h"
#include "threads/synch.h"


/* --- project 2 --- */
/* Process identifier. */
typedef int pid_t;
#define PID_ERROR ((pid_t) - 1)
/* --- project 2 --- */

void syscall_entry(void);
void syscall_handler(struct intr_frame *);
bool create(const char *file, unsigned initial_size);
bool do_remove(const char *file); 
unsigned tell(int fd);
struct file *fd_to_filep(int fd);
struct lock filesys_lock;

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

void syscall_init(void) {
  write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 | ((uint64_t)SEL_KCSEG)
                                                               << 32);
  write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

  /* The interrupt service rountine should not serve any interrupts
   * until the syscall_entry swaps the userland stack to the kernel
   * mode stack. Therefore, we masked the FLAG_FL. */
  write_msr(MSR_SYSCALL_MASK,
            FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
  lock_init(&filesys_lock);  // filesys_lock 초기화
}

/* The main system call interface */
void syscall_handler(struct intr_frame *f UNUSED) {
  /* 시스템 콜 번호 받기 */
  uint64_t number = f->R.rax;  // rax: 시스템 콜 번호
  /**
   * 인자가 들어오는 순서
   * 1) %rdi
   * 2) %rsi
   * 3) %rdx
   * 4) %r10
   * 5) %r8
   * 6) %r9
   */

  switch (number) {
    /* --- project 2 --- */
    case SYS_HALT:
      halt();
    case SYS_EXIT:
      exit(f->R.rdi);
      break;
    case SYS_FORK:
      f->R.rax = fork(f->R.rdi);
      break;
    case SYS_EXEC:
      f->R.rax = exec(f->R.rdi);
      break;
    case SYS_WAIT:
      f->R.rax = wait(f->R.rdi);
      break;
    case SYS_CREATE:
      f->R.rax = create(f->R.rdi, f->R.rsi);
      break;
    case SYS_REMOVE:
      f->R.rax = do_remove(f->R.rdi);
      break;
    case SYS_OPEN:
      f->R.rax = open(f->R.rdi);
      break;
    case SYS_FILESIZE:
      f->R.rax = filesize(f->R.rdi);
      break;
    case SYS_READ:
      f->R.rax = read(f->R.rdi,  f->R.rsi, f->R.rdx);
      break;
    case SYS_WRITE:
      f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
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
      thread_exit();
      break;
  }

  thread_exit();  // 프로세스 종료
}

/* --- project 2 ---- */
/* --- process 관련 함수 ---- */
void halt(void) { power_off(); }

/* 사용자 프로그램 종료 시스템 콜 */
void exit(int status) {
  struct thread *t = thread_current();

  /**
   * 1. 현재 사용자 프로그램 종료
   * 2. 커널에게 status 반환
   * 3. 프로세스 부모가 wait 하고 있는 경우 status 전달?
   */
  /**
   * 코드의 흐름: exit() -> thread_exit() -> process_exit()
   *
   */

  t->exit_status = status;
  thread_exit();
}

pid_t fork(const char *thread_name) {
  /**
   * [동작내용]
   * 1. 현재 프로세스를 복제한 자식 프로세스를 생성(이름: thread_name)
   *  -> %rbx, %rsp, %rbp, %r12 ~ %r15에 있는 데이터를 복제할 것:
   * pte_for_each_func 쓸 것
   * 2. 자식 프로세스의 pid 값을 반환할 것
   * 3. 자식 프로세스의 반환값은 0일 것
   * 4. 자식 프로세스는 fd와 가상메모리공간을 별도로 가질 것
   * 5. 부모 프로세스는 자식 프로세스가 완전히 포크되기 전까지는 반환하지 말 것
   * -> 동기화요소 도입 자식 프로세스가 자원을 복제하는 것에 실패 시 부모는
   * tid_error를 반환할 것
   *
   */

  // TODO: return process_fork(thread_name);
  return NULL;
}

int exec(const char *file) {
  // TODO:
}

int wait(pid_t pid) {
  // TODO:
  return process_wait(pid);
}
/* --- project 2 --- */
/* 파일 생성 */
bool create(const char *file, unsigned initial_size) {
  /**
   * 동작
   * 1. 파일 생성(이름: file, 크기: initial_size)
   * 2. 반환: 성공시 true, 실패시 false
   */
  check_address(file);  // 파일 이름의 주소 유효성 검사
  bool success =
      filesys_create(file, initial_size);  // 성공시 true, 실패시 false
  return success;
}
/* 파일 삭제 */
bool do_remove(const char *file) {
  check_address(file);
  bool success = filesys_remove(file);  //

  return success;
}

/* 주소 유효성 검사 */
void check_address(void *addr) {
  /**
   * 다음의 경우 user process 종료하기
   * 1. 사용자가 유효하지 않은 포인터 제공
   * 2. 커널메모리를 향하는 포인터 제공
   * 3. 커널영역에 해당하는 block 제공
   */
  struct thread *t = thread_current();

  if (!is_user_vaddr(addr) || addr == NULL ||
      pml4_get_page(t->pml4, addr) == NULL) {
    exit(-1);
  }
}

/* 파일 열기 */
int open(const char *file) {
  /**
   * 1. 파일 경로 유효성 검사
   * 2. 파일 접근 권한 확인
   * 3. 새 파일 디스크립터 할당
   * 4. 파일 열기 및 파일 테이블 업데이트
   * 5. 에러 처리
   * 6. 자식 프로세스 상속
   */
  check_address(file);                         // 경로 유효성 검사
  struct file *file_obj = filesys_open(file);  // 파일 열기

  // 파일이 제대로 생성되었는지 확인
  if (file_obj == NULL) {
    return -1;
  }
  // 만들어진 파일을 스레드 내 fdt 테이블에 추가
  int fd = add_file_to_fd_table(file_obj);

  // 파일을 열 수 없는 경우 -1 받기
  if (fd == -1) {
    file_close(file_obj);
  }
  return fd;
}

/* 파일 크기 */
int filesize(int fd) {
  struct file *fileobj = fd_to_filep(fd);
  return file_length(fileobj);
}
/* 파일 읽기 */
int read(int fd, void *buffer, unsigned size) {
  /**
   * 동작내용
   * 1. fd로 열린 파일을 통해 size만큼 buffer로 읽음
   * 2. 반환값: 실제로 읽은 byte 수, 0(파일의 끝) 또는 -1(읽는 도중 에러)
   * 3. fd가 0인 경우 키보드 입력을 받음: input_getc() 활용
   */
  // 유효 주소 검사
  check_address(buffer);
  check_address(buffer + size - 1);
  unsigned char *buf = buffer;
  int read_count;

  struct file *file_obj = fd_to_filep(fd);

  if (file_obj == NULL) {
    return -1;
  }

  /* STDIN */
  if (fd == STDIN_FILENO) {
    char key;
    for (int read_count = 0; read_count < size; read_count++) {
      key = input_getc();
      *buf++ = key;
      if (key == '\0') {
        break;
      }
    }
  }

  /* stdout 일 때 -1 반환 */
  else if (fd == STDOUT_FILENO) {
    return -1;
  } else {
    lock_acquire(&filesys_lock);
    read_count = file_read(file_obj, buffer, size);
    lock_release(&filesys_lock);
  }
  return read_count;
}
/* 파일 쓰기 */
int write(int fd, const void *buffer, unsigned size) {
  /** 동작내용
   * 1. buffer 로부터 size만큼 읽어서 fd에 쓰기
   * 2. 실제로 write 한 바이트의 수 반환하기
   */
  check_address(buffer);
  struct file *fileobj = fd_to_filep(fd);
  int read_count;
  if (fd == STDOUT_FILENO) {
    putbuf(buffer, size);
    read_count = size;
  } else if (fd == STDIN_FILENO) {
    return -1;
  } else {
    lock_acquire(&filesys_lock);
    read_count = file_write(fileobj, buffer, size);
    lock_release(&filesys_lock);
  }
}
/* 파일에서 다음에 읽거나 쓸 위치를 변경 */
void seek(int fd, unsigned position) {
  if (fd < 2) {
    return;
  }
  struct file *file = fd_to_filep(fd);
  check_address(file);
  if (file == NULL) {
    return;
  }
  file_seek(file, position);
}
/* 파일에서 다음에 읽어질 위치 반환 */
unsigned tell(int fd) {
  if (fd < 2) {
    return;
  }
  struct file *file = fd_to_filep(fd);
  check_address(file);
  if (file == NULL) {
    return;
  }
  return file_tell(fd);
}
/* 파일 닫기 */
void close(int fd) {
  struct file *file = fd_to_filep(fd);
  check_address(file);
  if (file == NULL) {
    return;
  }
  file_close(file);
}

/* 파일을 스레드의 fdt에 추가 */
int add_file_to_fd_table(struct file *file_obj) {
  struct thread *t = thread_current();
  struct file **fdt = t->file_descriptor_table;
  int fd = t->fdidx;  // fd값은 2부터 시작해야함(stdin, stdout 각각 0, 1임)

  while (t->file_descriptor_table[fd] != NULL && fd < FDT_COUNT_LIMIT) {
    fd++;
  }

  if (fd >= FDT_COUNT_LIMIT) {
    return -1;
  }
  t->fdidx = fd;
  fdt[fd] = file_obj;
  return fd;
}
/* fd 값으로 해당 파일을 반환 */
struct file *fd_to_filep(int fd) {
  if (fd < 0 || fd > FDT_COUNT_LIMIT) {
    return NULL;
  }
  struct thread *t = thread_current();
  struct file **fdt = t->file_descriptor_table;

  struct file *file = fdt[fd];
  return file;
}