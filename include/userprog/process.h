#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_create_initd(const char *file_name);
tid_t process_fork(const char *name, struct intr_frame *if_);
int process_exec(void *f_name);
int process_wait(tid_t);
void process_exit(void);
void process_activate(struct thread *next);

void argument_stack(char **parse, int count, void **rsp);

int process_add_file(struct file *file);
struct file *process_get_file(int fd);
void process_close_file(int fd);

struct thread *get_child_process(int pid);

struct container
{                         // lazy_load_segment에서 필요한 인자들
  struct file *file;      // 내용이 담긴 파일 객체
  off_t ofs;              // 이 페이지에서 읽기 시작한 위치
  size_t page_read_bytes; // 이 페이지에서 읽어야 하는 바이트 수
  size_t page_zero_bytes; // 이 페이지에서 read_bytes만큼 읽고 공간이 남아 0으로 채워야 하는 바이트 수
};

#endif /* userprog/process.h */
