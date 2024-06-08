#ifndef VM_FILE_H
#define VM_FILE_H
#include "filesys/file.h"
#include "vm/vm.h"

struct page;
enum vm_type;

struct file_page
{													// lazy_load_segment에서 필요한 인자들
	struct file *file;			// 내용이 담긴 파일 객체
	off_t ofs;							// 이 페이지에서 읽기 시작한 위치
	size_t page_read_bytes; // 이 페이지에서 읽어야 하는 바이트 수
	size_t page_zero_bytes; // 이 페이지에서 read_bytes만큼 읽고 공간이 남아 0으로 채워야 하는 바이트 수
};

void vm_file_init(void);
bool file_backed_initializer(struct page *page, enum vm_type type, void *kva);
void *do_mmap(void *addr, size_t length, int writable,
							struct file *file, off_t offset);
void do_munmap(void *va);
#endif
