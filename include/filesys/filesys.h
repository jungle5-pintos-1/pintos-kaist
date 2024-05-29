#ifndef FILESYS_FILESYS_H
#define FILESYS_FILESYS_H

#include <stdbool.h>
#include "filesys/off_t.h"

/* Sectors of system file inodes. */
#define FREE_MAP_SECTOR 0       /* Free map file inode sector. */
#define ROOT_DIR_SECTOR 1       /* Root directory file inode sector. */


/* Disk used for file system. */
extern struct disk *filesys_disk;

void filesys_init (bool format); // 파일시스템 초기화
void filesys_done (void); // 파일시스템 종료
bool filesys_create (const char *name, off_t initial_size); // 파일 생성
struct file *filesys_open (const char *name); // 파일 열기
bool filesys_remove (const char *name); // 파일 삭제

#endif /* filesys/filesys.h */
