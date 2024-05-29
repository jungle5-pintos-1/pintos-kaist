#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include "threads/synch.h"

void syscall_init(void);
struct lock filesys_lock;
void check_address(void *);
void halt(void);
void exit(int);
bool create(const char *, unsigned);
bool remove(const char *);
int open(const char *);
int filesize(int);
int read(int, void *, unsigned);
int write(int, void *, unsigned);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);
int exec(const char *cmd_line);
#endif /* userprog/syscall.h */
