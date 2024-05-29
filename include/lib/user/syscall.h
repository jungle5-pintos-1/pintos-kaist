#ifndef __LIB_USER_SYSCALL_H
#define __LIB_USER_SYSCALL_H

#include <debug.h>
#include <stdbool.h>
#include <stddef.h>

/* Process identifier. */
typedef int pid_t;
#define PID_ERROR ((pid_t) - 1)

/* Map region identifier. */
typedef int off_t;
#define MAP_FAILED ((void *)NULL)

/* Maximum characters in a filename written by readdir(). */
#define READDIR_MAX_LEN 14

/* Typical return values from main() and arguments to exit(). */
#define EXIT_SUCCESS 0 /* Successful execution. */
#define EXIT_FAILURE 1 /* Unsuccessful execution. */

extern struct lock filesys_lock; // 파일시스템 관련 lock


/* Projects 2 and later. */
void halt(void) NO_RETURN;  // power_off()를 호출하면서 핀토스 종료
void exit(int status) NO_RETURN;  // 현재 프로세스 종료, 커널에게 status 반환
pid_t fork(const char *thread_name);  // 현재 프로세스 클론
int exec(const char *file);           // 새로운 프로세스 실행
int wait(pid_t);  // pid_t 프로세스가 종료할 때까지 기다림 (pid_t는 자식
                  // 프로세스의 pid)
bool create(const char *file,
            unsigned initial_size);  // 파일생성(여는 로직은 open에서 다룸)
bool do_remove(const char *file);  // 파일 삭제(파일이 열렸는지 여부 무관)
int open(const char *file);  // 파일 열기 + file discriptor 반환
int filesize(int fd);        // 파일 디스크립터의 파일 크기 반환
int read(int fd, void *buffer, unsigned size);  // 파일 디스크립터로부터 length
                                                // 바이트만큼 읽기(버퍼에 기록)
int write(int fd, const void *buffer,
          unsigned size);  // 버퍼에서 fd로 lengh 바이트만큼 기록
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);
int add_file_to_fd_table(struct file *file_obj);  // 파일을 fdt에 추가

int dup2(int oldfd, int newfd);

/* Project 3 and optionally project 4. */
void *mmap(void *addr, size_t length, int writable, int fd, off_t offset);
void munmap(void *addr);

/* Project 4 only. */
bool chdir(const char *dir);
bool mkdir(const char *dir);
bool readdir(int fd, char name[READDIR_MAX_LEN + 1]);
bool isdir(int fd);
int inumber(int fd);
int symlink(const char *target, const char *linkpath);

static inline void *get_phys_addr(void *user_addr) {
  void *pa;
  asm volatile("movq %0, %%rax" ::"r"(user_addr));
  asm volatile("int $0x42");
  asm volatile("\t movq %%rax, %0" : "=r"(pa));
  return pa;
}

static inline long long get_fs_disk_read_cnt(void) {
  long long read_cnt;
  asm volatile("movq $0, %rdx");
  asm volatile("movq $1, %rcx");
  asm volatile("int $0x43");
  asm volatile("\t movq %%rax, %0" : "=r"(read_cnt));
  return read_cnt;
}

static inline long long get_fs_disk_write_cnt(void) {
  long long write_cnt;
  asm volatile("movq $0, %rdx");
  asm volatile("movq $1, %rcx");
  asm volatile("int $0x44");
  asm volatile("\t movq %%rax, %0" : "=r"(write_cnt));
  return write_cnt;
}

#endif /* lib/user/syscall.h */
