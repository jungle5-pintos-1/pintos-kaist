#ifndef THREADS_INTERRUPT_H
#define THREADS_INTERRUPT_H

#include <stdbool.h>
#include <stdint.h>

/* Interrupts on or off? */
enum intr_level {
	INTR_OFF,             /* Interrupts disabled. */
	INTR_ON               /* Interrupts enabled. */
};

enum intr_level intr_get_level (void);
enum intr_level intr_set_level (enum intr_level);
enum intr_level intr_enable (void);
enum intr_level intr_disable (void);

/* Interrupt stack frame. */
/**
 * @brief
 * 일반목적 레지스터
 */
struct gp_registers {
	uint64_t r15;
	uint64_t r14;
	uint64_t r13;
	uint64_t r12;
	uint64_t r11;
	uint64_t r10;
	uint64_t r9;
	uint64_t r8;
	uint64_t rsi;
	uint64_t rdi;
	uint64_t rbp;
	uint64_t rdx;
	uint64_t rcx;
	uint64_t rbx;
	uint64_t rax;
} __attribute__((packed));

/**
 * @brief
 * 인터럽트 발생시 시스템의 상태를 저장하기 위한 자료구조
 * 
 * @details
 * 인터럽트 발생 시 시스템은 cpu는 현재 실행 중인 명령어의 위치와 현재 레지스터 상태를 저장해야함
 */
struct intr_frame {
	/* Pushed by intr_entry in intr-stubs.S.
	   These are the interrupted task's saved registers. */
	struct gp_registers R; // 일반목적 레지스터(general purpose register)
	uint16_t es; // 세그먼트 레지스터
	uint16_t __pad1;
	uint32_t __pad2;
	uint16_t ds; // 세그먼트 레지스터(데이터)
	uint16_t __pad3;
	uint32_t __pad4;
	/* Pushed by intrNN_stub in intr-stubs.S. */
	uint64_t vec_no; /* Interrupt vector number. */
/* Sometimes pushed by the CPU,
   otherwise for consistency pushed as 0 by intrNN_stub.
   The CPU puts it just under `eip', but we move it here. */
	uint64_t error_code;
/* Pushed by the CPU.
   These are the interrupted task's saved registers. */
	uintptr_t rip; // 인터럽트가 발생 시점의 명령어의 위치 저장
	uint16_t cs; // 코드 세그먼트 레지스터
	uint16_t __pad5;
	uint32_t __pad6;
	uint64_t eflags; // 레지스터의 상태
	uintptr_t rsp; // 인터럽트가 발생한 시점의 스택포인터 저장
	uint16_t ss; // 스택 세그먼트 레지스터
	uint16_t __pad7;
	uint32_t __pad8;
} __attribute__((packed));

typedef void intr_handler_func (struct intr_frame *);

void intr_init (void);
void intr_register_ext (uint8_t vec, intr_handler_func *, const char *name);
void intr_register_int (uint8_t vec, int dpl, enum intr_level,
                        intr_handler_func *, const char *name);
bool intr_context (void);
void intr_yield_on_return (void);

void intr_dump_frame (const struct intr_frame *);
const char *intr_name (uint8_t vec);

#endif /* threads/interrupt.h */