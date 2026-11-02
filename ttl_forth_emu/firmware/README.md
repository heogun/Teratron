# 네이티브 FORTH 펌웨어

## 진단 / 부팅

`build.py`와 `assembler.py`는 **기존 Gigatron ISA만** 사용해 512KiB 이미지를 생성합니다.
실행 중 Python 콜백, 특별 FORTH opcode, 호스트 문자열 파서로 명령을 대행하지 않습니다.
전원 인가 시 하드웨어 DMA가 플래시 전체를 I-RAM에 복사한 뒤 PC 0에서 시작합니다.

부팅 코드는 다음을 실제 CPU 명령으로 수행합니다:

- 데이터 RAM 패턴 쓰기/읽기, 별도 I-RAM 위치 쓰기/읽기 후 복원
- 플래시 AA/55/90 소프트웨어 ID 명령과 BF/B7 확인, F0로 복귀
- framebuffer 초기화, 글꼴 렌더링, `TTL FORTH / OK` 출력
- PS/2 set-2 프레임 수신 래치에서 스캔 코드 읽기, make/break/shift 처리

RAM ABI는 이미지와 함께 생성하는 `forth.json`에 있습니다. `diag` bit0/1/2는
RAM/I-RAM/Flash ID 진단, bit3은 키 입력 수신, bit4는 VGA 프레임 서비스 완료입니다.
진단 실패 시 `status=error=255`로 멈춥니다.

VGA는 네이티브 OUT 명령으로 수평 동기·수직 동기·160픽셀 데이터를 생성합니다.
각 활성 라인은 200 native tick이고, 논리 픽셀 행을 물리 4행에 반복합니다.
5×8 문자 셀 32×15개를 사용합니다. 화면이 차면 전체를 지우고 위에서 다시 출력합니다.
키보드는 프레임 서비스 사이에 폴링합니다. **현재는 협력형 화면 갱신**이므로 FORTH 계산이나
SAVE 중에는 VGA 갱신이 멈춥니다. 전체 워크로드에서 연속 VGA 타이밍을 보장하는 실시간 ROM은 아닙니다.

실행 방법은 [프로젝트 README](../README.md), 장치/전원 모델은 [에뮬레이션 계약](../docs/emulation.md)을 참고하세요.

## 최소 16비트 FORTH

```forth
123 456 + .
: SQUARE DUP * ;
12 SQUARE .
: CHOOSE IF 111 ELSE 222 THEN ;
0 CHOOSE .
: COUNT BEGIN 1 - DUP 0= UNTIL ;
5 COUNT .
: FACT DUP 1 U< IF DROP 1 ELSE DUP 1 - RECURSE * THEN ;
5 FACT .
```

각각 579, 144, 222, 0, 120을 출력합니다. 대소문자는 구분하지 않습니다.
한 줄은 최대 126자, 토큰/워드 이름은 최대 15자이며 공백으로 구분합니다. 정의는 여러 줄에 걸쳐 작성할 수 있습니다.
수는 10진수로 입력하고 16비트 모듈러 연산을 합니다. `.`은 signed 16비트로 표시합니다.
비교 결과는 참 `$FFFF`, 거짓 0입니다. `U<`는 unsigned 비교입니다.

### 워드

| 종류 | 워드 |
| --- | --- |
| 스택 | `DUP DROP SWAP OVER DEPTH` |
| 산술/논리 | `+ - * AND OR XOR INVERT = 0= U<` |
| 출력/검색 | `. EMIT CR WORDS` |
| 데이터 RAM | `@ ! C@ C!` |
| I-RAM 바이트 | `IC@ ( addr bank -- byte )`, `IC! ( byte addr bank -- )` |
| 플래시 읽기 | `FC@ ( addr bank -- byte )` |
| 정의/실행 | `: ; RECURSE EXECUTE` |
| 제어 흐름 | `IF ELSE THEN BEGIN UNTIL AGAIN` |
| 영속화 | `SAVE` |

`@/!`의 셀 저장 순서는 little-endian입니다. 코드 이미지의 native 16비트 명령어 저장 순서는 big-endian입니다.
`IC@/IC!/FC@`는 공유 포트를 한 네이티브 루틴에서 접근합니다. NEXT도 포트를 쓰므로,
일반 `C!` 여러 개로 포트 주소와 데이터를 따로 설정하면 중간에 래치가 덮어써집니다.
일반 사용자의 플래시 쓰기 워드는 `SAVE`뿐입니다.

예를 들어 임시 RAM은 아래처럼 사용합니다. 데이터는 SAVE에 포함되지 않습니다.

```forth
12345 24576 !
24576 @ .
```

이 구현은 **최소 token-threaded FORTH**이지 완전한 ANS FORTH가 아닙니다.
호스트가 아니라 네이티브 CPU의 NEXT 루프가 I-RAM 토큰을 읽고 실행합니다.
`: ... ;`는 I-RAM에 새 헤더와 토큰 코드를 기록하며, native 명령어로 직접 최적화하는 컴파일러는 아닙니다.
현재는 사용자 정의 `IMMEDIATE`, 문자열/주석 문법, `VARIABLE/CREATE/DOES>`, 파일 시스템,
분할 컴파일, `/MOD`, `DO/LOOP`를 제공하지 않습니다. `EXECUTE`는 코드 바이트 주소를 받으며 tick (`'`) 검색 워드는 없습니다.
임의 메모리 쓰기는 보호되지 않으므로 펌웨어/스택/MMIO를 덮어쓰면 머신이 손상될 수 있습니다.

### RAM / I-RAM 배치

| 영역 | 용도 |
| --- | --- |
| RAM `$0020` 이후 zero page | 펌웨어 상태, private helper 반환 주소 |
| RAM `$0200`, `$0300` | 데이터 스택/VM 리턴 스택, 각각 최대 120셀 |
| RAM `$0500` | 입력 줄 |
| RAM `$0700`, `$0800` | 컴파일 제어 흐름 스택과 타입, 최대 16단계 |
| RAM `$4000`–`$4FFF` | 4KiB 출력 기록 링버퍼 |
| RAM `$6000`–`$7FFD` | 사용자 임시 작업 공간; `$7FFE`는 부팅 RAM 진단 위치 |
| RAM `$8000`–`$F7FF` | 픽셀 행당 256바이트, 앞 160바이트가 표시되는 framebuffer |
| I-RAM 바이트 `$00000` 이후 | 네이티브 펌웨어, 실행 뱅크 0 |
| I-RAM `$18000` / `$19000` | 글꼴 / PS/2 normal·shift 테이블 |
| I-RAM 바이트 뱅크 2 | dictionary: root 0–5, 헤더/토큰은 `$0100`–`$FCFF` |

dictionary header는 link(2바이트) + 이름 길이/즉시 실행 bit7 + 이름 + 토큰 코드입니다.
LIT/CALL/BRANCH/0BRANCH의 16비트 피연산자는 little-endian이며 EXIT가 워드를 끝냅니다.
`;`에서 root head/here를 갱신하여 게시합니다. 실패한 정의는 폐기하고 스택을 초기화합니다.
오류는 `?`와 RAM error 코드로 보고합니다: 1=토큰 길이, 2=잘못된 토큰, 3=컴파일 상태,
4=스택 범위, 5=이름/숫자 해석, 6=dictionary 용량, 7=플래시 검증, 8=잘못된 뱅크입니다.
`IF`와 `BEGIN`의 제어 흐름 타입을 구분하며, 짝이 맞지 않으면 정의를 게시하지 않습니다.

### 실제 펌웨어 저장 경로

```forth
: ANSWER 42 ;
SAVE
```

SAVE는 CPU를 멈추거나 호스트에 복사를 요청하지 않습니다. I-RAM에서 실행하며
AA/55/80 + AA/55/10 Chip Erase, AA/55/A0 Byte Program,
DQ6 토글 폴링을 수행합니다. 전체 **512KiB I-RAM**을 저장하고, 전체를 다시 읽어 검증합니다.
데이터 RAM은 저장하지 않습니다. 부팅 시 I-RAM의 dictionary root로 정의를 다시 찾습니다.

SAVE 이후 정의는 재부팅을 견디며, SAVE 이후 추가로 정의한 워드는 저장하지 않으면 사라집니다.
전원 감지/자동 저장 루틴은 없습니다. **SAVE 도중 비정상 전원 차단은 정의하지 않습니다.**
저장 시간은 전체 이미지/프로그램 바이트 수에 따라 달라지며, 완료 전에는 전원을 유지해야 합니다.

### 검증

```sh
ctest --test-dir ttl_forth_emu/build --output-on-failure
ctest --test-dir ttl_forth_emu/build -R '^firmware$' --output-on-failure
```

Python 통합 테스트는 RAM을 조작해 FORTH 결과를 꾸미지 않고 PS/2 입력만으로 연산·메모리·정의·조건문·루프·재귀·오류 복구를 검사합니다.
실제 CPU SAVE, 마지막 I-RAM 바이트의 저장, RAM 소실, 저장/미저장 정의의 재부팅 동작,
GUI HTTP/단일 스텝/키보드 전달도 검증합니다. 전체 이미지 저장 테스트 때문에 실행 시간이 걸릴 수 있습니다.
