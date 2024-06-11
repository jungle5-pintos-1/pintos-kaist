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

bool lazy_load_segment(struct page *page, void *aux);

/* FRAME에 내용을 load할 때 필요한 정보 */
struct lazy_load_arg {
    struct file *file;      // 내용이 담긴 file 객체
    off_t ofs;              // PAGE에서 읽기 시작할 위치
    uint32_t read_bytes;    // PAGE에서 읽어야 하는 byte 수
    uint32_t zero_bytes;    // PAGE에서 read_bytes만큼 읽고 공간이 남아 0으로 채워야 하는 byte 수
};

#endif /* userprog/process.h */
