#ifndef FILESYS_FILE_H
#define FILESYS_FILE_H

#include "filesys/off_t.h"

struct inode;

/* Opening and closing files. */
struct file *file_open (struct inode *); // 파일 열기
struct file *file_reopen (struct file *); // 파일 다시 열기
struct file *file_duplicate (struct file *file); // 파일 객체 복제
void file_close (struct file *); // 파일 닫기
struct inode *file_get_inode (struct file *); // 파일 inode 반환

/* Reading and writing. */
off_t file_read (struct file *, void *, off_t); // 파일 읽기
off_t file_read_at (struct file *, void *, off_t size, off_t start); // off_set 위치에서 파일 읽기
off_t file_write (struct file *, const void *, off_t); // 파일 쓰기
off_t file_write_at (struct file *, const void *, off_t size, off_t start); // off_set 지점에서 파일 쓰기

/* Preventing writes. */
void file_deny_write (struct file *); // 
void file_allow_write (struct file *);

/* File position. */
void file_seek (struct file *, off_t);
off_t file_tell (struct file *);
off_t file_length (struct file *);

#endif /* filesys/file.h */
