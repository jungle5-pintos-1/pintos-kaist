#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* See [8254] for hardware details of the 8254 timer chip. */

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* Number of timer ticks since OS booted. */
static int64_t ticks;

/* Number of loops per timer tick.
	 Initialized by timer_calibrate(). */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops(unsigned loops);
static void busy_wait(int64_t loops);
static void real_time_sleep(int64_t num, int32_t denom);

/* Sets up the 8254 Programmable Interval Timer (PIT) to
	 interrupt PIT_FREQ times per second, and registers the
	 corresponding interrupt. */
// 8254 타이머 칩을 설정하여 주어진 주파수로 인터럽트 발생
// 타이머 인터럽트를 시스템에 등록
void timer_init(void)
{
	/* 8254 input frequency divided by TIMER_FREQ, rounded to
		 nearest. */
	uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;

	outb(0x43, 0x34); /* CW: counter 0, LSB then MSB, mode 2, binary. */
	outb(0x40, count & 0xff);
	outb(0x40, count >> 8);

	intr_register_ext(0x20, timer_interrupt, "8254 Timer");
}

/* Calibrates loops_per_tick, used to implement brief delays. */
// loops_per_tick 값을 조정하여, 시스템의 성능에 따라 적절한 지연을 생성할 수 있는 루프의 수를 결정
// 간단한 반복 루프를 사용하여 시간 지연을 보다 정밀하게 조정
void timer_calibrate(void)
{
	unsigned high_bit, test_bit;

	ASSERT(intr_get_level() == INTR_ON);
	printf("Calibrating timer...  ");

	/* Approximate loops_per_tick as the largest power-of-two
		 still less than one timer tick. */
	loops_per_tick = 1u << 10;
	while (!too_many_loops(loops_per_tick << 1))
	{
		loops_per_tick <<= 1;
		ASSERT(loops_per_tick != 0);
	}

	/* Refine the next 8 bits of loops_per_tick. */
	high_bit = loops_per_tick;
	for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
		if (!too_many_loops(high_bit | test_bit))
			loops_per_tick |= test_bit;

	printf("%'" PRIu64 " loops/s.\n", (uint64_t)loops_per_tick * TIMER_FREQ);
}

/* Returns the number of timer ticks since the OS booted. */
// 운영 체제가 부팅된 이후로 발생한 타이머 틱의 수를 반환
int64_t
timer_ticks(void)
{
	enum intr_level old_level = intr_disable();
	int64_t t = ticks;
	intr_set_level(old_level);
	barrier();
	return t;
}

/* Returns the number of timer ticks elapsed since THEN, which
	 should be a value once returned by timer_ticks(). */
// 주어진 시점부터 경과한 타이머 틱의 수를 계산
int64_t
timer_elapsed(int64_t then)
{
	return timer_ticks() - then;
}

/* Suspends execution for approximately TICKS timer ticks. */
// 지정된 타이머 틱 동안 실행을 일시 중지
// void timer_sleep(int64_t ticks)
// {
// 	int64_t start = timer_ticks();

// 	ASSERT(intr_get_level() == INTR_ON);
// 	while (timer_elapsed(start) < ticks)
// 		thread_yield();
// }

void timer_sleep(int64_t ticks)
{
	int64_t start = timer_ticks();

	ASSERT(intr_get_level() == INTR_ON);
	if (timer_elapsed(start) < ticks)
		thread_sleep(start + ticks); // 지정된 ticks 시간이 경과하지 않았으면 thread_sleep을 호출하여 스레드 대기
}

/* Suspends execution for approximately MS milliseconds. */
// 밀리초 단위 지정 시간동안 실행을 일시 중지
void timer_msleep(int64_t ms)
{
	real_time_sleep(ms, 1000);
}

/* Suspends execution for approximately US microseconds. */
// 마이크로초
void timer_usleep(int64_t us)
{
	real_time_sleep(us, 1000 * 1000);
}

/* Suspends execution for approximately NS nanoseconds. */
// 나노초
void timer_nsleep(int64_t ns)
{
	real_time_sleep(ns, 1000 * 1000 * 1000);
}

/* Prints timer statistics. */
void timer_print_stats(void)
{
	printf("Timer: %" PRId64 " ticks\n", timer_ticks());
}

/* Timer interrupt handler. */
// 타이머 인터럽트가 발생할 때마다 호출되어 'ticks' 변수를 증가시키고, 스레드 관리자를 통해 스레드의 타이밍을 조정
static void
timer_interrupt(struct intr_frame *args UNUSED)
{
	ticks++;
	thread_tick();
	if (thread_mlfqs)
	{
		mlfqs_increment_recent_cpu();
		if (ticks % 4 == 0)
		{
			mlfqs_recalculate_priority();
			if (ticks % TIMER_FREQ == 0) // TIMER_FREQ : 1초에 몇 개의 ticks가 실행되는지 (초기값 : 100)
			{
				mlfqs_recalculate_recent_cpu();
				mlfqs_calculate_load_avg();
			}
		}
	}
	thread_wake_up(ticks);
}

/* Returns true if LOOPS iterations waits for more than one timer
	 tick, otherwise false. */
static bool
too_many_loops(unsigned loops)
{
	/* Wait for a timer tick. */
	int64_t start = ticks;
	while (ticks == start)
		barrier();

	/* Run LOOPS loops. */
	start = ticks;
	busy_wait(loops);

	/* If the tick count changed, we iterated too long. */
	barrier();
	return start != ticks;
}

/* Iterates through a simple loop LOOPS times, for implementing
	 brief delays.

	 Marked NO_INLINE because code alignment can significantly
	 affect timings, so that if this function was inlined
	 differently in different places the results would be difficult
	 to predict. */
// 지정된 횟수만큼 루프를 실행하여 짧은 지연을 만듭니다.
// 함수는 인라인 방지 속성을 사용하여 코드 정렬이 타이밍에 미치는 영향을 최소화
static void NO_INLINE
busy_wait(int64_t loops)
{
	while (loops-- > 0)
		barrier();
}

/* Sleep for approximately NUM/DENOM seconds. */
static void
real_time_sleep(int64_t num, int32_t denom)
{
	/* Convert NUM/DENOM seconds into timer ticks, rounding down.

		 (NUM / DENOM) s
		 ---------------------- = NUM * TIMER_FREQ / DENOM ticks.
		 1 s / TIMER_FREQ ticks
		 */
	int64_t ticks = num * TIMER_FREQ / denom;

	ASSERT(intr_get_level() == INTR_ON);
	if (ticks > 0)
	{
		/* We're waiting for at least one full timer tick.  Use
			 timer_sleep() because it will yield the CPU to other
			 processes. */
		timer_sleep(ticks);
	}
	else
	{
		/* Otherwise, use a busy-wait loop for more accurate
			 sub-tick timing.  We scale the numerator and denominator
			 down by 1000 to avoid the possibility of overflow. */
		ASSERT(denom % 1000 == 0);
		busy_wait(loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
	}
}
