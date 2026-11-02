# 에뮬레이션 계약과 검증 범위

## 확정된 동작

| 구성 | 기능 모델 |
| --- | --- |
| CPU | 기본 Gigatron ISA, 8비트 ALU, 16비트 명령어, 6.25MHz 논리 clock |
| 데이터 RAM | 64KiB 고정, 뱅크 없음. AS6C4008의 A16–A18 접지 |
| I-RAM | 512KiB: 실행은 64K워드 뱅크 4개, 바이트 접근은 64KiB 뱅크 8개 |
| 플래시 | SST39SF040, 512KiB 전부 I-RAM 이미지에 사용 |
| 부팅 | tick당 1바이트 DMA, 524288 tick 동안 CPU 정지, 완료 후 PC 0 실행 |
| 전원 재인가 | RAM/레지스터/키보드/화면/오디오 상태 초기화, 플래시는 보존 |
| 저장 | 명시적 FORTH SAVE만 소거·프로그램·검증 수행; 데이터 RAM은 제외 |

RAM의 전원 인가 초기값은 재현성을 위해 0으로 정합니다. 실제 SRAM 초기값은 보장하지 않습니다.
`cpu.cycles`는 실행한 native 명령어 수, `ticks`는 DMA를 포함한 보드 clock 수입니다.
명령 실행 후 VGA, 오디오를 같은 tick에서 갱신합니다. branch delay slot은 유지합니다.
프로그램은 Megatron 전용 IO/CE 확장이 아니라 **기본 Gigatron ISA**를 사용합니다.
정의되지 않은 ST/RAM-bus 조합은 결정적 0을 저장하며, 물리적 floating bus를 재현하지 않습니다.

이는 명령/장치 수준 모델입니다. 게이트 전파 지연, SRAM 동시 읽기/쓰기 충돌, 세부 fetch 래치의
전기적 경합, 전압·온도·노이즈·부품 손상은 모델링하지 않습니다.

## 버스 / 레지스터

instruction fetch는 독립된 I-RAM 경로를 사용합니다. 데이터 창의 기본값은 RAM입니다.
MEMORY_BANK는 I-RAM/플래시 바이트 접근에만 영향을 주며 데이터 RAM 주소에는 영향을 주지 않습니다.
래치 변경은 다음 접근부터 적용되고, EXEC_BANK는 다음 instruction fetch부터 적용됩니다.

| 주소 | 이름 | 읽기 / 쓰기 |
| --- | --- | --- |
| `$FF00` | MEMORY_BANK | I-RAM/플래시 A16–A18, 하위 3비트 |
| `$FF01` | EXEC_BANK | I-RAM 워드 주소 상위 2비트 |
| `$FF02` | WINDOW | 0=RAM, 1=I-RAM, 2=플래시; 다른 값 쓰기는 무시 |
| `$FF03` | KEY_DATA | 스캔 코드 읽고 READY 해제; 비어 있으면 0; 쓰기는 무시 |
| `$FF04` | KEY_STATUS | bit0 READY, bit1 FRAME_ERROR, bit2 OVERRUN; bit1/2는 1을 써서 해제 |
| `$FF05` | 예약 | 읽기는 0, 쓰기는 무시; 전원 감지 기능 없음 |
| `$FF06` | FLASH_STATUS | 에뮬레이터 진단용 BUSY(bit0)/잘못된 0→1 시도(bit1); bit1은 1 쓰기로 해제 |
| `$FF07` | PORT_LO | 포트 바이트 주소 A0–A7 |
| `$FF08` | PORT_HI | 포트 바이트 주소 A8–A15 |
| `$FF09` | PORT_DATA | MEMORY_BANK와 포트 주소가 가리키는 바이트 읽기/쓰기 |
| `$FF0A` | PORT_TARGET | 1=I-RAM, 2=플래시; 다른 값 쓰기는 무시 |

레지스터는 모든 창에서 공통입니다. `$FF00`–`$FF0A`에 가려진 **데이터 RAM 11바이트는 일반 CPU
메모리로 접근하지 못합니다**. I-RAM/플래시는 PORT_DATA 경로로 가려진 위치까지 접근할 수 있습니다.
펌웨어는 WINDOW=0으로 스택을 유지하고 PORT_DATA를 사용합니다. 실제 TTL 구현에는 포트 주소
래치 2개와 경로 선택 회로가 필요합니다. 이 인터페이스는 검증된 최종 회로가 아닌 구현용 설계안입니다.

I-RAM 바이트 주소 a는 워드 a/2에 대응하며 짝수 바이트가 opcode, 홀수가 operand입니다.
두 바이트 쓰기는 원자적이지 않습니다. 현재 실행 중인 명령어를 덮어쓰지 않아야 합니다.

## PS/2 / VGA / 오디오

- PS/2 하강 에지에서 시작 0, LSB 우선 8비트, 홀수 패리티, 정지 1을 검증합니다.
  idle-high clock은 무시하며 미완성 프레임은 2ms 뒤 오류 처리합니다.
- 수신 래치는 **1바이트**입니다. 미소비 중 새 프레임은 버리고 OVERRUN을 설정합니다.
  KEY_DATA와 CPU IN-bus 읽기는 모두 래치를 소비하며, 조건이 거짓인 분기도 IN-bus를 읽습니다.
- E0/F0 접두어는 그대로 전달합니다. ASCII 변환과 Shift make/break 처리는 펌웨어에서 수행합니다.
  키보드 송신/LED/host inhibit와 전기적 74HC595 회로는 지원 범위에 없습니다.
- OUT 하위 6비트 BBGGRR을 640×480 RGBA framebuffer로 변환하며 native 픽셀당 가로 4픽셀입니다.
  짧은 scanline도 다음 행을 밀어내지 않도록 실제 row/column 좌표로 기록합니다.
- HSYNC 상승 에지에서 AC를 XOUT에 래치합니다. 상위 4비트 DAC를 44.1kHz로 샘플링하고 DC를 제거합니다.
  4보이스 합성은 펌웨어의 역할이며 현재 최소 FORTH 펌웨어에는 없습니다.

## 플래시 / 전원

기준은 [로컬 SST39SF040 데이터시트](../../datasheet/SST39SF040-70-4C-PHE.pdf)의 표 4/12, 그림 9입니다.

- AA@$5555 → 55@$2AAA 뒤 A0+데이터는 Byte Program, 80+잠금 해제+30은 4KiB Sector Erase,
  80+잠금 해제+10@$5555는 Chip Erase입니다. 명령 주소는 A0–A14로 비교합니다.
- 바이트는 1→0만 프로그램합니다. Byte Program/Sector Erase/Chip Erase는 각각 20µs/25ms/100ms의 최대값으로
  결정적으로 지연합니다: 125/156250/625000 tick. 완료 전에는 기존 array bytes를 유지합니다.
- busy 동안 쓰기/F0 reset은 무시합니다. DQ7은 데이터 반전/erase 중 0, DQ6는 매 읽기마다 토글하며
  각 작업의 첫 DQ6는 1입니다. Software ID는 전체 주소 0/1에서 BF/B7입니다. F0로 ID 모드를 나옵니다.
- SST39SF040에 별도 BUSY 출력 핀이 있다고 가정하지 않습니다. 펌웨어는 FLASH_STATUS가 아니라
  실제 플래시 데이터의 **DQ6**를 폴링합니다.

**SAVE를 실행하지 않은 전원 차단은 플래시를 바꾸지 않습니다.** 다음 부팅은 기존 플래시를 복원하고
휘발성 정의/데이터를 버립니다. 전원 이상 감지·종료 인터럽트·홀드업 커패시터·자동 SAVE는 없습니다.
**SAVE 도중 비정상 종료는 정의하지 않으며**, 손상·기존 이미지 보존·복구·칩 수명을 보장하지 않습니다.
busy 중 재부팅/파일 내보내기 거부는 UI 안전장치일 뿐, 비정상 차단 복구 보장은 아닙니다.

네이티브 GUI의 정상 종료 시 파일 기록은 **이미 프로그램된 플래시 배열만** 원자적 rename으로
내보내는 호스트 기능입니다. I-RAM→플래시 저장을 실행하지 않습니다. 브라우저/CLI는 명시적 Export를 사용합니다.
에뮬레이터를 강제 종료하거나 SAVE 중 죽인 경우의 결과는 지원 범위 밖입니다.

## 검증 / 남은 경계

`ctest --test-dir ttl_forth_emu/build --output-on-failure`로 다음을 검사합니다.

- 65536개 명령 인코딩 × 6개 경계 레지스터 상태 = 393216개 ISA 사례, 분기 지연·버스 읽기·X++·ST 목적 레지스터.
- DMA 정지/복원, 모든 뱅크·포트, 고정 RAM, 미저장 재부팅, 플래시 보호/ID/DQ6/지연.
- PS/2 parity/start/stop/overrun/timeout, 양쪽 Shift, VGA 좌표/리셋과 오디오.
- 실제 PS/2 입력으로 FORTH 연산·컴파일·루프·재귀·오류 복구·native SAVE·저장 후 부팅.
- CLI 파일 왕복, 브라우저 HTTP 제어, 네이티브 컨트롤러 입력·스텝·브레이크포인트·파일 기록.

네이티브 실행기의 `--smoke-test`는 별도로 실제 macOS Metal 창을 열어 SQUARE=144를 확인하고 종료합니다.
사용자 프로필을 읽거나 쓰지 않으며 디스플레이가 필요하므로 기본 CTest에는 포함하지 않습니다.

현재 최소 FORTH는 **계산/저장 중 VGA 갱신을 멈추는 협력형**입니다. 이는 펌웨어의 제약이며
에뮬레이터가 하드웨어 대신 화면이나 FORTH 결과를 만들어 주지 않습니다. 연속 VGA 실시간 커널,
완전한 ANS FORTH, 물리 TTL 배선/160ns 타이밍은 완료했다고 주장하지 않습니다. Windows/Linux는 보장 대상이 아닙니다.
