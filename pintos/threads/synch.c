/* 이 파일은 교육용 운영체제 Nachos의 소스 코드에서 파생되었다.
   Nachos 저작권 고지는 아래에 전문을 그대로 싣는다. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* 세마포어 SEMA를 VALUE로 초기화한다. 세마포어는 음수가 아닌
   정수 값과 이를 조작하는 두 원자적 연산으로 이루어진다.

   - down 또는 "P": 값이 양수가 될 때까지 기다린 뒤 값을 감소시킨다.

   - up 또는 "V": 값을 증가시키고, 기다리는 스레드가 있으면 하나를
   깨운다. */
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* 세마포어에 대한 down 또는 "P" 연산. SEMA의 값이 양수가 될 때까지
   기다린 뒤 원자적으로 값을 감소시킨다.

   이 함수는 잠들 수 있으므로 인터럽트 핸들러 안에서 호출해서는 안 된다.
   인터럽트가 비활성화된 상태에서 호출할 수는 있지만, 잠들게 되면 다음에
   스케줄되는 스레드가 인터럽트를 다시 켤 가능성이 높다. 이것은
   sema_down 함수이다. */
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	while (sema->value == 0) {
		list_push_back (&sema->waiters, &thread_current ()->elem);
		thread_block ();
	}
	sema->value--;
	intr_set_level (old_level);
}

/* 세마포어에 대한 down 또는 "P" 연산이지만, 세마포어가 이미 0이
   아닐 때만 수행한다. 세마포어가 감소되면 true를, 그렇지 않으면
   false를 반환한다.

   이 함수는 인터럽트 핸들러에서 호출할 수 있다. */
bool
sema_try_down (struct semaphore *sema) {
	enum intr_level old_level;
	bool success;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level (old_level);

	return success;
}

/* 세마포어에 대한 up 또는 "V" 연산. SEMA의 값을 증가시키고,
   SEMA를 기다리는 스레드가 있으면 그중 하나를 깨운다.

   이 함수는 인터럽트 핸들러에서 호출할 수 있다. */
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (!list_empty (&sema->waiters))
		thread_unblock (list_entry (list_pop_front (&sema->waiters),
					struct thread, elem));
	sema->value++;
	intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* 두 스레드 사이에서 제어가 "핑퐁"처럼 오가게 하는 세마포어
   자체 테스트. 동작을 확인하려면 printf() 호출을 넣어 보라. */
void
sema_self_test (void) {
	struct semaphore sema[2];
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up (&sema[0]);
		sema_down (&sema[1]);
	}
	printf ("done.\n");
}

/* sema_self_test()에서 사용하는 스레드 함수. */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
	}
}

/* LOCK을 초기화한다. 락은 어느 시점에도 최대 하나의 스레드만 보유할 수
   있다. 이 락은 "재귀적"이지 않다. 즉, 현재 락을 보유한 스레드가 같은
   락을 다시 획득하려 하면 오류이다.

   락은 초기값이 1인 세마포어의 특수한 형태이다. 락과 그런 세마포어의
   차이는 두 가지이다. 첫째, 세마포어는 1보다 큰 값을 가질 수 있지만
   락은 한 번에 하나의 스레드만 소유할 수 있다. 둘째, 세마포어에는
   소유자가 없으므로 한 스레드가 세마포어를 "down"하고 다른 스레드가
   "up"할 수 있지만, 락에서는 같은 스레드가 획득과 해제를 모두 해야
   한다. 이러한 제약이 부담스럽다면 락 대신 세마포어를 사용해야 한다는
   좋은 신호이다. */
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

/* LOCK을 획득한다. 필요하다면 사용 가능해질 때까지 잠든다. 현재
   스레드가 이미 이 락을 보유하고 있으면 안 된다.

   이 함수는 잠들 수 있으므로 인터럽트 핸들러 안에서 호출해서는 안 된다.
   인터럽트가 비활성화된 상태에서 호출할 수는 있지만, 잠들어야 한다면
   인터럽트는 다시 켜진다. */
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));

	sema_down (&lock->semaphore);
	lock->holder = thread_current ();
}

/* LOCK 획득을 시도하고, 성공하면 true를 실패하면 false를 반환한다.
   현재 스레드가 이미 이 락을 보유하고 있으면 안 된다.

   이 함수는 잠들지 않으므로 인터럽트 핸들러 안에서 호출할 수 있다. */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* 현재 스레드가 소유하고 있어야 하는 LOCK을 해제한다. 이것은
   lock_release 함수이다.

   인터럽트 핸들러는 락을 획득할 수 없으므로, 인터럽트 핸들러 안에서
   락을 해제하려 하는 것은 의미가 없다. */
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));

	lock->holder = NULL;
	sema_up (&lock->semaphore);
}

/* 현재 스레드가 LOCK을 보유하고 있으면 true를, 그렇지 않으면 false를
   반환한다. (다른 스레드가 락을 보유하는지 검사하는 것은 경쟁 상태를
   일으킬 수 있음에 유의하라.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* 리스트 안의 세마포어 하나. */
struct semaphore_elem {
	struct list_elem elem;              /* 리스트 원소. */
	struct semaphore semaphore;         /* 이 세마포어. */
};

/* 조건 변수 COND를 초기화한다. 조건 변수는 코드의 한 부분이 조건을
   알리고, 협력하는 다른 코드가 그 신호를 받아 처리할 수 있게 한다. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

/* LOCK을 원자적으로 해제하고, 다른 코드가 COND에 신호를 보낼 때까지
   기다린다. COND에 신호가 오면 반환하기 전에 LOCK을 다시 획득한다.
   이 함수를 호출하기 전에는 LOCK을 보유하고 있어야 한다.

   이 함수가 구현하는 모니터는 "Hoare" 방식이 아니라 "Mesa" 방식이다.
   즉, 신호를 보내는 동작과 받는 동작은 원자적 연산이 아니다. 따라서
   일반적으로 호출자는 대기가 끝난 뒤 조건을 다시 확인하고, 필요하다면
   다시 기다려야 한다.

   주어진 조건 변수는 하나의 락에만 연결되지만, 하나의 락은 여러 조건
   변수에 연결될 수 있다. 즉, 락에서 조건 변수로의 일대다 대응이 있다.

   이 함수는 잠들 수 있으므로 인터럽트 핸들러 안에서 호출해서는 안 된다.
   인터럽트가 비활성화된 상태에서 호출할 수는 있지만, 잠들어야 한다면
   인터럽트는 다시 켜진다. */
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);
	list_push_back (&cond->waiters, &waiter.elem);
	lock_release (lock);
	sema_down (&waiter.semaphore);
	lock_acquire (lock);
}

/* LOCK으로 보호되는 COND에서 기다리는 스레드가 있으면, 이 함수는 그중
   하나에 신호를 보내 대기에서 깨운다. 이 함수를 호출하기 전에는 LOCK을
   보유하고 있어야 한다.

   인터럽트 핸들러는 락을 획득할 수 없으므로, 인터럽트 핸들러 안에서
   조건 변수에 신호를 보내려 하는 것은 의미가 없다. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	if (!list_empty (&cond->waiters))
		sema_up (&list_entry (list_pop_front (&cond->waiters),
					struct semaphore_elem, elem)->semaphore);
}

/* LOCK으로 보호되는 COND에서 기다리는 모든 스레드가 있으면 모두 깨운다.
   이 함수를 호출하기 전에는 LOCK을 보유하고 있어야 한다.

   인터럽트 핸들러는 락을 획득할 수 없으므로, 인터럽트 핸들러 안에서
   조건 변수에 신호를 보내려 하는 것은 의미가 없다. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}
