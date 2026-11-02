# TTL FORTH 컴퓨터: 하드웨어 에뮬레이터

`../gigatron_emu/core/`의 C CPU·VGA·오디오 코드를 복사하여 확장한 독립 프로젝트입니다.
기존 에뮬레이터와 ROM 소스는 변경하지 않습니다. CMake·C11·Python 3.8+로 코어/펌웨어를 빌드합니다.
기본 활성화된 네이티브 GUI는 C++20과 OS 그래픽/오디오 라이브러리를 추가로 사용하며 외부 패키지 다운로드는 없습니다.
라이선스와 원저작권 표기는 `LICENSE`에 유지했습니다.

**진단 펌웨어 → 최소 GUI → 최소 FORTH 펌웨어**를 구현했습니다.
펌웨어는 네이티브 CPU에서 PS/2를 해석하고, 화면을 그리며, FORTH를 해석·컴파일하고,
플래시를 저장합니다. GUI/Python은 FORTH를 대신 실행하지 않습니다.
`--demo`만은 기존 부팅 확인용 짧은 프로그램이며 FORTH가 아닙니다.

## 빌드 및 실행

워크스페이스 루트에서:

```sh
cmake -S ttl_forth_emu -B ttl_forth_emu/build -DCMAKE_BUILD_TYPE=Debug
cmake --build ttl_forth_emu/build --parallel
ctest --test-dir ttl_forth_emu/build --output-on-failure
ttl_forth_emu/build/ttl_forth_emu --demo --cycles 20
```

### FORTH와 GUI 실행

```sh
# 기존 gigatron_emu/frontend처럼 독립 창으로 실행하는 네이티브 GUI
ttl_forth_emu/build/frontend/sokol_imgui/ttl_forth_sokol_imgui
# 선택 사항: 브라우저 GUI (출력된 http://127.0.0.1:8765 열기)
python3 ttl_forth_emu/gui/server.py
# GUI 없이 실제 PS/2 입력 → 네이티브 펌웨어 실행
python3 ttl_forth_emu/gui/run.py --eval ': SQUARE DUP * ;' --eval '12 SQUARE .'
# 네이티브 SAVE 후 현재 플래시를 파일로 내보내기
python3 ttl_forth_emu/gui/run.py --eval ': ANSWER 42 ;' --eval SAVE --export machine.flash
python3 ttl_forth_emu/gui/run.py --load machine.flash --eval 'ANSWER .'
```

네이티브 GUI는 `frontend/sokol_imgui/`에 있으며, 기존 `gigatron_emu/3rd_party/`의 Sokol/ImGui를
읽기 전용으로 재사용합니다. macOS는 Objective-C++/Metal, Windows는 D3D11,
Linux는 OpenGL/X11/ALSA를 사용합니다. macOS에서 빌드 및 실제 창 실행을 검증했습니다.
Linux 헤드리스 환경 등에서 GUI 의존성을 제외하려면 configure에 `-DTTL_BUILD_NATIVE_GUI=OFF`를 지정하세요.
GUI 실행 파일 옆에 펌웨어를 자동 복사하므로 특정 작업 디렉터리에서 실행할 필요가 없습니다.
네이티브 창의 입력/단축키/디버거 사용법은 [`frontend/sokol_imgui/README.md`](frontend/sokol_imgui/README.md)에 있습니다.

상세 단계별 구현, 워드 목록, 메모리 배치와 저장 제약은 [`firmware/README.md`](firmware/README.md)를 참고하세요.
GUI의 **펌웨어 SAVE**는 CPU 명령으로 저장하며, **플래시 내보내기**는 이미 저장된 현재 플래시를 다운로드할 뿐입니다.
전원 재인가는 데이터 RAM을 지웁니다. 정의한 워드는 SAVE 후에만 재부팅을 견딥니다.
`gui/run.py --power-fail --export machine.flash`로 전원 이상 감지 후 네이티브 저장도 실행할 수 있습니다.

데모는 데이터 RAM `$0000`에 `$42`를 기록합니다. 출력에 `ram[0]=42`가 표시됩니다.
단일 테스트: `ctest --test-dir ttl_forth_emu/build -R '^cpu$' --output-on-failure`.

```sh
# 디버거 기능으로 현재 I-RAM을 플래시에 저장하고 파일로 내보내기
ttl_forth_emu/build/ttl_forth_emu --demo --cycles 20 --checkpoint machine.flash
# 새 전원 인가: 플래시 → I-RAM 복원 후 실행
ttl_forth_emu/build/ttl_forth_emu --flash machine.flash --cycles 20
# PS/2 set-2 스캔 코드 1개 주입, 전원 이상 신호, 프레임버퍼 덤프
ttl_forth_emu/build/ttl_forth_emu --demo --scan 1c --power-fail-at 10 --frame frame.ppm
```

`--flash`는 정확히 524288바이트의 원시 이미지이며 명령어는 **상위 바이트, 하위 바이트** 순서입니다.
기존 128KiB Gigatron ROM은 그대로 입력할 수 없습니다. 새 펌웨어 이미지는 CMake가 `build/firmware/forth.flash`에 생성합니다.
`--cycles`와 `--power-fail-at`은 DMA 부팅 이후 기준이며, 전체 보드 tick에는 부팅 시간이 포함됩니다.
주입한 키를 읽지 않는 펌웨어는 키를 소비하지 않습니다. 데모는 VGA 타이밍을 생성하지 않아 화면 덤프는 검정입니다.

## 확정 사항과 구현용 설계안

상위 명세에 없던 **I-RAM 용량, 레지스터 배치, 쓰기 경로, PS/2 상태 신호**는 아래와 같이
정한 구현용 설계안입니다. 실제 TTL 회로의 배선/타이밍 검증을 대신하지 않습니다.

| 구분 | 모델 |
|---|---|
| CPU | Gigatron 네이티브 ISA, 8비트 ALU/데이터, 16비트 명령어, 6.25MHz |
| I-RAM | 512KiB = 256K × 16비트, 64K워드 실행 뱅크 4개 |
| 데이터 RAM | **64KiB 고정**, 뱅크 없음. AS6C4008의 A16–A18 접지 |
| 플래시 | SST39SF040, 512KiB 전체에 I-RAM만 저장 |
| 부팅 | CPU를 정지시킨 채 tick당 1바이트 복사: 524288 tick, 약 83.9ms |
| 데이터 RAM 전원 인가 | 저장/복원하지 않음. 테스트 재현성을 위해 0으로 초기화; 실제 SRAM 초기값은 미정 |
| 화면/음성 | 기존 OUT 기반 VGA 모델과 XOUT 상위 4비트 DAC 모델 재사용 |

정상 실행에서 tick당 네이티브 명령어 하나를 실행하고 VGA → 오디오를 갱신합니다.
기존 코어의 branch delay slot을 유지합니다. 이식 과정에서 즉시값 버스의 mode-7 X 증가 및
ST의 X/Y 목적 레지스터에 AC가 기록되는 동작은 `gigatron_rom/Docs/gtemu.c`를 기준으로 보정했습니다.
화면 생성과 PS/2 폴링은 펌웨어가 수행합니다. 4보이스 wavetable 합성 펌웨어는 아직 없으며 DAC 모델만 있습니다.
게임패드, GT1 직렬 로더, Arduino/MCU 보조 장치는 포함하지 않습니다.

## 버스와 레지스터

명령 fetch는 독립된 I-RAM 경로를 사용합니다. 데이터 버스의 64KiB 창은 아래 `WINDOW`로 선택합니다.
기본값은 데이터 RAM입니다. 플래시와 I-RAM 바이트 창은 동일한 `MEMORY_BANK` 래치를 사용합니다.
뱅크 전환은 다음 접근부터 적용됩니다. 실행 뱅크는 별도 래치이며 다음 명령 fetch부터 적용됩니다.

| 주소 | 이름 | 읽기 / 쓰기 |
|---|---|---|
| `$FF00` | MEMORY_BANK | I-RAM/플래시 바이트 접근 A16–A18, 하위 3비트 |
| `$FF01` | EXEC_BANK | I-RAM 워드 접근 상위 주소, 하위 2비트 |
| `$FF02` | WINDOW | 0=데이터 RAM, 1=I-RAM, 2=플래시; 다른 값의 쓰기는 무시 |
| `$FF03` | KEY_DATA | 스캔 코드 읽기 및 READY 해제; 비어 있으면 0; 쓰기는 무시 |
| `$FF04` | KEY_STATUS | bit0=READY, bit1=FRAME_ERROR, bit2=OVERRUN; bit1/2는 1을 써서 해제 |
| `$FF05` | POWER_STATUS | bit0=전원 이상 감지; 다음 전원 인가까지 유지; 쓰기는 무시 |
| `$FF06` | FLASH_STATUS | bit0=BUSY, bit1=불가능한 0→1 프로그램 시도; bit1은 1을 써서 해제 |
| `$FF07` | PORT_LO | 포트 접근용 바이트 주소 A0–A7 래치 |
| `$FF08` | PORT_HI | 포트 접근용 바이트 주소 A8–A15 래치 |
| `$FF09` | PORT_DATA | MEMORY_BANK와 포트 주소로 지정한 I-RAM/플래시 바이트 읽기·쓰기 |
| `$FF0A` | PORT_TARGET | 1=I-RAM, 2=플래시; 다른 값의 쓰기는 무시 |

레지스터는 모든 창에 공통으로 나타납니다. 따라서 **각 바이트 뱅크의 `$FF00`–`$FF0A`는 예약**이며
이 창을 통해 그 위치의 RAM/I-RAM/플래시에 직접 접근할 수 없습니다. 물리 메모리 용량과
CPU에서 일반 메모리로 사용할 수 있는 주소 수는 다릅니다. 새 PORT_DATA 경로로는 I-RAM/플래시의 예약 위치까지 모두 접근할 수 있습니다.
부팅 DMA, 디버거 체크포인트와 네이티브 SAVE는 전체 I-RAM 영역을 복사합니다.
그 외 주소는 선택한 창의 일반 메모리입니다. `MEMORY_BANK` 변경은 데이터 RAM 주소에 영향을 주지 않습니다.

펌웨어는 데이터 RAM 스택을 유지하기 위해 WINDOW=0을 유지하고 PORT_DATA로 I-RAM/플래시에 접근합니다.
이 추가 포트는 실제 회로에 주소 래치 2개와 경로 선택 회로가 필요하다는 **하드웨어 인터페이스 설계 변경**입니다.
FLASH_STATUS는 에뮬레이터 진단 레지스터이며 SST39SF040에 직접적인 BUSY 출력 핀이 있다고 가정하지 않습니다.
네이티브 펌웨어는 이 레지스터 대신 실제 플래시 데이터의 DQ6 토글을 폴링합니다.

I-RAM의 바이트 주소 `a`는 워드 `a / 2`에 대응합니다. 짝수 주소는 opcode 상위 바이트,
홀수 주소는 operand 하위 바이트입니다. 두 바이트 쓰기는 원자적이지 않으므로 컴파일할 때
현재 실행 중인 명령어를 덮어쓰지 않아야 합니다. 뱅크마다 별도 스택이 생기는 구조가 아닙니다.

## PS/2

`ttl_ps2_lines()`는 PS/2 clock의 하강 에지에서 data를 샘플링합니다.
시작 0 + LSB 우선 8비트 + 홀수 패리티 + 정지 1의 11비트 프레임을 검증합니다.
중단된 프레임은 2ms 뒤 오류 처리됩니다. `ttl_inject_scan()`은 동일한 수신 경로를 쓰는 테스트 편의 함수입니다.

수신 버퍼는 **1바이트 래치**입니다. 미소비 상태에서 새 코드가 오면 기존 코드를 유지하고 OVERRUN을 설정합니다.
`E0`, `F0` 등 접두어를 포함한 스캔 코드는 그대로 전달하며 ASCII로 바꾸지 않습니다.
CPU의 IN 버스 읽기도 KEY_DATA와 같은 소비 동작을 합니다. READY를 먼저 확인해야 합니다.
수신은 비동기이며 현재 펌웨어는 프레임 갱신 사이에 읽습니다. 화면 갱신은 협력형이며 계산/저장 중 정지할 수 있습니다.
키보드 송신 명령/LED 제어/호스트 inhibit 및 실제 74HC595 회로의 전기적 모델은 아직 없습니다.
READY·오류 검출·소비 신호에는 향후 TTL 제어 회로 설계가 필요합니다.

## 플래시, 전원 이상, 저장

플래시 모델은 잠금 해제 `AA@$5555 → 55@$2AAA` 뒤의 명령을 처리합니다:

- `$A0` + 목적 주소의 데이터: Byte Program, 비트는 **1→0만** 변경.
- `$80` + 잠금 해제 재실행 + `$30`@섹터 주소: 4KiB Sector Erase.
- `$80` + 잠금 해제 재실행 + `$10@$5555`: Chip Erase.
- `$90`: 소프트웨어 ID 모드 (`BF`, `B7`); `$F0`: 읽기 모드 복귀.
- BUSY 동안 쓰기는 무시하고, 읽기는 DQ7 폴링 및 DQ6 토글을 반환합니다.

지연은 결정적 모델값으로 Byte Program 125 tick(20µs), Sector Erase 156250 tick(25ms),
Chip Erase 625000 tick(100ms)을 사용합니다. 전압·온도·셀 열화·명령 사이 시간 제한은 모델링하지 않습니다.
명령 주소 비교에는 A0–A14를 사용합니다. 회로 설계 전에는
[SST39SF040 데이터시트](https://ww1.microchip.com/downloads/en/DeviceDoc/20005022C.pdf)의 전기/타이밍 조건을 별도 검증해야 합니다.

`ttl_power_fail()`은 상태 래치만 설정합니다. **자동 저장 엔진이나 CPU 인터럽트를 가정하지 않습니다.**
현재 펌웨어는 프롬프트 및 FORTH 토큰 경계에서 이를 폴링하고, I-RAM에서 실행하면서 위 프로토콜로 플래시를 지워 씁니다.
실제 전원 단절 시 중단된 erase/program의 결과 및 홀드업 커패시터의 방전은 아직 모델링하지 않습니다.

`ttl_checkpoint()` / `--checkpoint`는 별도의
**정지된 머신의 디버거 저장 도구**입니다. 전체 I-RAM을 지운 플래시에 바이트 단위로 기록하고
명령 시퀀스와 BUSY 지연을 사용하지만, 그동안 CPU/VGA/오디오는 진행하지 않습니다.
이미지가 거의 모두 프로그램 대상이면 모델상 약 10.6초가 필요하므로 실제 전원 유지 시간은 별도 설계 과제입니다.
출력 파일은 임시 파일에 완전히 쓴 후 rename하며, 실행 중인 플래시 작업이 있으면 체크포인트/재부팅은 거부합니다.
데이터 RAM은 체크포인트에 절대 포함하지 않습니다.

## 검증 범위

CTest는 DMA 동안 CPU 정지, I-RAM 전체 복원/뱅킹, 고정 64KiB RAM, 네이티브 CPU의
메모리맵 접근/자기 수정 코드/분기 지연 슬롯, 플래시 보호·프로그램·삭제·ID·BUSY,
PS/2 프레임·오버런·타임아웃, 전원 상태 래치, I-RAM 저장 후 재부팅과 RAM 소실,
VGA 프레임 에지 및 오디오 샘플, CLI 이미지 파일 왕복을 검사합니다.
추가 Python 통합 테스트는 실제 PS/2 입력으로 산술·스택·RAM 접근·런타임 정의·조건문·루프·재귀,
오류 복구, 네이티브 SAVE와 실행 중 전원 이상 저장, 저장 후 재부팅, GUI HTTP 제어를 검증합니다.
실제 TTL 회로의 160ns 타이밍, 보장된 연속 VGA 출력, 전원 유지 회로의 안전성 검증은 아닙니다.
