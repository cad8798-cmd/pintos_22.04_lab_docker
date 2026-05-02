#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
static void sys_exit (int status);

/* 시스템 콜.
 *
 * 이전에는 시스템 콜 서비스가 인터럽트 핸들러에서 처리되었다
 * (예: linux의 int 0x80). 하지만 x86-64에서는 제조사가 시스템 콜을
 * 요청하기 위한 효율적인 경로인 `syscall` 명령어를 제공한다.
 *
 * syscall 명령어는 Model Specific Register (MSR)의 값을 읽어서 동작한다.
 * 자세한 내용은 매뉴얼을 참고하라. */

#define MSR_STAR 0xc0000081         /* 세그먼트 선택자 MSR */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL 대상 */
#define MSR_SYSCALL_MASK 0xc0000084 /* eflags에 대한 마스크 */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* syscall_entry가 사용자 영역 스택을 커널 모드 스택으로 교체하기 전까지
	 * 인터럽트 서비스 루틴은 어떤 인터럽트도 처리해서는 안 된다.
	 * 따라서 FLAG_FL을 마스크했다. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/*sys_exit이 필요해서 여기다 씀.*/
static void
sys_exit (int status) {
	struct thread *curr = thread_current ();

	curr->exit_status = status;
	thread_exit ();
}


/* 주 시스템 콜 인터페이스 */
void
syscall_handler (struct intr_frame *f) {
	// TODO: 여기에 구현을 작성하세요.
	//printf ("system call!\n");
	//thread_exit ();
	//위의 주석들은 원래 존재하던것. switch부터 밑에는 작성한것.
	switch (f->R.rax) {
		case SYS_EXIT:
			sys_exit ((int)f->R.rdi);
			break;
		default:
			thread_exit();
			break;
	}
}
