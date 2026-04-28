#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* 8254 타이머 칩의 하드웨어 세부 사항은 [8254]를 참고한다. */

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* OS가 부팅된 이후의 타이머 틱 수. */
static int64_t ticks;

static int64_t global_ticks = INT64_MAX;
static struct thread *idle_thread;
static struct list sleep_list;
static struct list ready_list;


/* 타이머 틱 하나당 루프 횟수.
   timer_calibrate()에서 초기화된다. */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);

/* 8254 Programmable Interval Timer(PIT)가 초당 PIT_FREQ번 인터럽트를
   발생시키도록 설정하고, 해당 인터럽트를 등록한다. */
void
timer_init (void) {

	list_init(&sleep_list); //? 어디서 동작함?
	/* 8254 입력 주파수를 TIMER_FREQ로 나눈 값을 가장 가까운 값으로 반올림한다. */
	uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;

	outb (0x43, 0x34);    /* CW: 카운터 0, LSB 다음 MSB, 모드 2, 바이너리. */
	outb (0x40, count & 0xff);
	outb (0x40, count >> 8);

	intr_register_ext (0x20, timer_interrupt, "8254 Timer");
}

/* 짧은 지연을 구현하는 데 사용하는 loops_per_tick을 보정한다. */
void
timer_calibrate (void) {
	unsigned high_bit, test_bit;

	ASSERT (intr_get_level () == INTR_ON);
	printf ("Calibrating timer...  ");

	/* loops_per_tick을 타이머 틱 하나보다 작은 가장 큰 2의 거듭제곱으로
	   근사한다. */
	loops_per_tick = 1u << 10;
	while (!too_many_loops (loops_per_tick << 1)) {
		loops_per_tick <<= 1;
		ASSERT (loops_per_tick != 0);
	}

	/* loops_per_tick의 다음 8비트를 더 정밀하게 조정한다. */
	high_bit = loops_per_tick;
	for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
		if (!too_many_loops (high_bit | test_bit))
			loops_per_tick |= test_bit;

	printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* OS가 부팅된 이후의 타이머 틱 수를 반환한다. */
int64_t
timer_ticks (void) {
	enum intr_level old_level = intr_disable ();
	int64_t t = ticks;
	intr_set_level (old_level);
	barrier ();
	return t;
}

/* THEN 이후 경과한 타이머 틱 수를 반환한다.
   THEN은 이전에 timer_ticks()가 반환한 값이어야 한다. */
int64_t
timer_elapsed (int64_t then) {
	return timer_ticks () - then;
}

/* 대략 TICKS 타이머 틱 동안 실행을 중단한다. */
void
timer_sleep (int64_t ticks) {
	int64_t start = timer_ticks ();

	struct thread *t = thread_current ();

	if (ticks == 0) {
		thread_yield();
		return;
	}

	else if (ticks < 0) {
		return;
	}
	// global_ticks = t->local_tick;

	 //일단 지금은 block 쓰는데, 나중에 동기화 중 하나 써야됨.//
	// 여기서 현재 스레드를 어떻게 알 수 있는가... 

	ASSERT (intr_get_level () == INTR_ON);

	if (timer_elapsed (start) < ticks) {
		if (t != idle_thread) {
			enum intr_level old_level = intr_disable ();
			t->local_tick = start + ticks;
			if (t->local_tick <= global_ticks) 
				global_ticks = t->local_tick;
			// list_remove(&t->elem);

			struct list_elem *e;
			for (e = list_begin(&sleep_list); e != list_end(&sleep_list); e = list_next(e))
    			if (list_entry(e, struct thread, elem)->local_tick > t->local_tick)
        			break;

			list_insert(e, &t->elem);
			thread_block();
			// intr_enable ();
			intr_set_level(old_level);
			
		}
	}
	// thread_yield();



	// while (timer_elapsed (start) < ticks)
	// 	thread_yield ();
}

/* 대략 MS 밀리초 동안 실행을 중단한다. */
void
timer_msleep (int64_t ms) {
	real_time_sleep (ms, 1000);
}

/* 대략 US 마이크로초 동안 실행을 중단한다. */
void
timer_usleep (int64_t us) {
	real_time_sleep (us, 1000 * 1000);
}

/* 대략 NS 나노초 동안 실행을 중단한다. */
void
timer_nsleep (int64_t ns) {
	real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* 타이머 통계를 출력한다. */
void
timer_print_stats (void) {
	printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}

/* 타이머 인터럽트 핸들러. */
static void
timer_interrupt (struct intr_frame *args UNUSED) {
	ticks++;
	thread_tick ();

	if (list_empty(&sleep_list) || !global_ticks)
		return;
	
	struct thread *t = list_entry (list_front (&sleep_list), struct thread, elem);

	while (t->local_tick <= timer_ticks ()) {
		if (list_empty(&sleep_list))
			return;

		enum intr_level old_level = intr_disable();
		list_pop_front(&sleep_list);

		thread_unblock(t);
		if (!list_empty(&sleep_list))
			t = list_entry (list_front (&sleep_list), struct thread, elem);

		//global_ticks = list_front(&sleep_list); //이거의 tick을 저장해야되는거긴해 근데 지금 global ticks 쓰지도 않는데 왜? 왜하는거지
		// intr_enable ();
		intr_set_level(old_level);
	}
}

/* LOOPS번 반복하는 동안 타이머 틱 하나보다 오래 기다리면 true를,
   그렇지 않으면 false를 반환한다. */
static bool
too_many_loops (unsigned loops) {
	/* 타이머 틱을 기다린다. */
	int64_t start = ticks;
	while (ticks == start)
		barrier ();

	/* LOOPS번 루프를 실행한다. */
	start = ticks;
	busy_wait (loops);

	/* 틱 카운트가 바뀌었다면 너무 오래 반복한 것이다. */
	barrier ();
	return start != ticks;
}

/* 짧은 지연을 구현하기 위해 단순 루프를 LOOPS번 반복한다.

   코드 정렬이 타이밍에 큰 영향을 줄 수 있으므로 NO_INLINE으로 표시한다.
   이 함수가 위치마다 다르게 인라인되면 결과를 예측하기 어려워진다. */
static void NO_INLINE
busy_wait (int64_t loops) {
	while (loops-- > 0)
		barrier ();
}

/* 대략 NUM/DENOM초 동안 잠든다. */
static void
real_time_sleep (int64_t num, int32_t denom) {
	/* NUM/DENOM초를 타이머 틱으로 변환하되, 내림한다.

	   (NUM / DENOM) s
	   ---------------------- = NUM * TIMER_FREQ / DENOM ticks.
	   1 s / TIMER_FREQ ticks
	   */
	int64_t ticks = num * TIMER_FREQ / denom;

	ASSERT (intr_get_level () == INTR_ON);
	if (ticks > 0) {
		/* 최소 한 개의 완전한 타이머 틱을 기다린다.
		   CPU를 다른 프로세스에 양보할 수 있도록 timer_sleep()을 사용한다. */
		timer_sleep (ticks);
	} else {
		/* 그렇지 않으면 한 틱보다 짧은 시간을 더 정확히 맞추기 위해
		   바쁜 대기 루프를 사용한다. 오버플로 가능성을 피하려고
		   분자와 분모를 1000으로 줄인다. */
		ASSERT (denom % 1000 == 0);
		busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
	}
}
