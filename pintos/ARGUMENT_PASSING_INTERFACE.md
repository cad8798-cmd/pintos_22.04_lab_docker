# Pintos Argument Passing: Parsing Interface Agreement

## 목적

`args-*` 테스트 구현을 분업하기 전에, 커맨드라인 파싱 담당자와 유저 스택 구성 담당자 사이의 인터페이스를 합의한다.

핵심은 다음과 같다.

> 파싱 담당자는 커맨드라인을 토큰 배열로 만든다.  
> 스택 담당자는 그 토큰 배열을 유저 스택 ABI 형태로 배치한다.

## 전체 흐름

```c
struct parsed_command cmd;

if (!parse_command_line (file_name, &cmd))
  goto done;

file = filesys_open (cmd.program_name);
if (file == NULL)
  goto done;

/* ELF load ... */

if (!setup_stack (if_))
  goto done;

if (!setup_arguments (if_, &cmd))
  goto done;
```

## 데이터 구조

```c
#define MAX_ARGS 128

struct parsed_command {
  int argc;
  char *argv[MAX_ARGS];
  char *program_name;
};
```

## 파싱 함수 인터페이스

```c
static bool
parse_command_line (char *cmdline, struct parsed_command *cmd);
```

### 입력

```text
cmdline
  - 커널 메모리에 있는 수정 가능한 문자열
  - 예: "args-multiple some arguments for you!"
```

### 출력

```text
cmd->argc
  - 인자 개수
  - args-none이면 1

cmd->argv[i]
  - cmdline 내부 토큰을 가리키는 커널 포인터
  - argv[0]은 실행 파일 이름
  - 연속 공백은 무시

cmd->program_name
  - cmd->argv[0]과 같음
  - filesys_open()에 넘길 실행 파일명
```

### 실패 조건

```text
- 인자가 너무 많을 때
- 유효한 토큰이 하나도 없을 때
```

## 파싱 예시

### args-none

```c
parse_command_line ("args-none", &cmd);
```

결과:

```text
cmd.argc = 1
cmd.argv[0] = "args-none"
cmd.program_name = "args-none"
```

### args-dbl-space

```c
parse_command_line ("args-dbl-space two  spaces!", &cmd);
```

결과:

```text
cmd.argc = 3
cmd.argv[0] = "args-dbl-space"
cmd.argv[1] = "two"
cmd.argv[2] = "spaces!"
cmd.program_name = "args-dbl-space"
```

## 중요한 주의점

`parse_command_line()`이 만든 `cmd.argv[i]`는 유저 주소가 아니다.

```text
cmd.argv[i]
  = 커널 메모리 안의 문자열 포인터
  = 아직 유저 프로그램이 접근할 수 없음
```

따라서 스택 담당자는 이 문자열들을 반드시 유저 스택에 복사해야 한다.

## 스택 구성 함수 인터페이스

```c
static bool
setup_arguments (struct intr_frame *if_,
                 const struct parsed_command *cmd);
```

### 입력

```text
if_
  - setup_stack() 이후 rsp가 USER_STACK으로 세팅된 intr_frame

cmd
  - parse_command_line()이 만든 argc/argv 정보
```

### 동작

```text
1. cmd->argv 문자열들을 유저 스택에 복사
2. 각 문자열의 유저 주소 저장
3. argv[argc] = NULL 배치
4. argv[0] ... argv[argc - 1] 포인터 배열 배치
5. 8바이트 정렬 맞춤
6. if_->R.rdi = argc
7. if_->R.rsi = argv의 유저 주소
```

## 역할 분담

### A 담당자: 커맨드라인 파싱

```text
- struct parsed_command 정의
- parse_command_line() 구현
- filesys_open(cmd.program_name)로 load() 수정
```

### B 담당자: 유저 스택 구성

```text
- setup_arguments() 구현
- 문자열 복사
- argv 포인터 배열 배치
- 8바이트 정렬
- if_->R.rdi, if_->R.rsi 설정
```

### C 담당자: exit syscall / 종료 메시지

```text
- SYS_EXIT 처리
- exit status 저장
- "process_name: exit(status)" 출력
```

## 합의해야 하는 핵심 계약

```text
A는 cmd.argv[i]를 커널 문자열 포인터 배열로 넘긴다.

B는 그 문자열들을 유저 스택에 복사한다.

B는 최종 유저 argv 주소를 if_->R.rsi에 넣는다.

B는 argc 값을 if_->R.rdi에 넣는다.
```

## 피해야 할 설계

```text
- 파싱 함수가 유저 스택까지 직접 건드리는 구조
- 스택 구성 함수가 커맨드라인 문자열을 다시 파싱하는 구조
- filesys_open()에 전체 커맨드라인을 넘기는 구조
- 연속 공백을 빈 인자로 처리하는 구조
```

## 요약

```text
parse_command_line()
  커맨드라인 문자열을 커널 argv 배열로 변환

setup_arguments()
  커널 argv 배열을 유저 스택 ABI 형태로 변환

load()
  위 두 단계를 호출하고 ELF 로딩 흐름을 관리
```
