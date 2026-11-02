# 네이티브 Sokol + Dear ImGui 에뮬레이터

기존 `gigatron_emu/frontend/sokol_imgui/`처럼 별도 OS 창을 여는 프런트엔드입니다.
새 컴퓨터의 `ttl_machine` 코어와 FORTH 펌웨어를 실행하며, 원본 에뮬레이터 파일은 수정하지 않습니다.
`gigatron_emu/3rd_party/`에 이미 있는 Sokol/ImGui 소스를 사용합니다.

## 실행

워크스페이스 루트에서:

```sh
cmake -S ttl_forth_emu -B ttl_forth_emu/build -DTTL_BUILD_NATIVE_GUI=ON
cmake --build ttl_forth_emu/build --parallel
ttl_forth_emu/build/frontend/sokol_imgui/ttl_forth_sokol_imgui
```

512KiB 이미지 경로를 첫 인자로 지정할 수도 있습니다. 기본 펌웨어는 실행 파일 옆
`firmware/forth.flash`에 자동 복사되며, 실행 작업 디렉터리에 의존하지 않습니다.
펌웨어가 바뀌면 CMake 재빌드로 이미지와 생성된 `forth_abi.h`를 함께 갱신합니다.

- macOS: C++20 / Objective-C++ / Metal, Cocoa·QuartzCore·MetalKit·AudioToolbox. 실제 빌드/창 실행 검증 완료.
- Windows: D3D11. Linux: OpenGL, X11/XInput2/Xcursor, ALSA, pthread 개발 라이브러리 필요.
- Windows/Linux 소스 경로도 추가했지만 이 환경에서 해당 플랫폼 실행은 검증하지 않았습니다.
- 네이티브 창이 필요 없다면 `-DTTL_BUILD_NATIVE_GUI=OFF`로 코어/펌웨어/브라우저 GUI만 빌드합니다.

## 조작

| 기능 | 조작 |
|---|---|
| 실행/정지 | Run/Pause 또는 F5 |
| 단일 스텝 | Step 또는 F6; DMA 중에는 부팅 바이트 한 개 진행 |
| 전원 재인가 | Power on 또는 F9; I-RAM 복원, 데이터 RAM 소실 |
| 전원 이상 신호 | Power-fail signal 또는 F10; CPU가 감지 후 직접 SAVE |
| 플래시 열기 | 경로 입력 후 Load flash, 또는 파일 드래그 앤 드롭 |
| 플래시 내보내기 | 출력 경로와 Export flash; 현재 플래시만 저장 |
| 영속화 | Firmware SAVE 버튼 또는 FORTH `SAVE` 입력 |

VGA 화면을 클릭하거나 `Send keyboard to machine`을 켜면 ASCII 문자/Enter/Backspace를
PS/2 set-2 프레임으로 전달합니다. UI 텍스트 필드 입력 중에는 머신으로 중복 전달하지 않습니다.
명령 입력창으로 한 줄을 전송하거나 예제 버튼을 사용할 수도 있습니다.

```forth
: SQUARE DUP * ;
12 SQUARE .
```

결과는 **144**입니다. 출력 기록은 RAM 링버퍼 관찰이며 별도 직렬 장치가 아닙니다.
VGA 화면은 실제 CPU OUT 신호의 framebuffer입니다. 계산/SAVE 중 갱신이 멈추는 것은 현재
협력형 펌웨어의 제약입니다. 오디오는 CPU XOUT DAC 모델의 샘플을 Sokol push API로 재생하며
4보이스 합성 펌웨어를 추가한 것은 아닙니다.

## 디버깅 / 저장

- CPU 레지스터, 다음 PC, 실행 뱅크, 현재 명령어, 포트/PS2/전원 상태, dictionary 위치를 표시합니다.
- PC 브레이크포인트는 실행 뱅크 0–3과 PC를 함께 지정합니다. 해당 명령어 실행 **직전** 멈추며 Step은 한 번 우회합니다.
- 데이터/VM 리턴 스택을 표시합니다. 메모리 탭은 RAM, I-RAM, 플래시 **물리 바이트**를 읽기 전용으로 표시합니다.
- 이미지와 메모리 관찰은 키를 소비하거나 플래시 DQ6를 토글하는 MMIO 읽기를 수행하지 않습니다.
- SAVE는 CPU에서 실행합니다. 완료 전 Load/Reset/Export를 거부하며, 임시 파일에 완전히 쓴 후 rename으로 내보냅니다.
- Export만 누르면 저장하지 않은 I-RAM 정의는 파일에 포함되지 않습니다. 먼저 SAVE가 끝날 때까지 기다려야 합니다.
- 호스트 키보드 큐는 입력 속도 조절용일 뿐, 실제 PS/2 수신 버퍼는 계속 1바이트입니다.
- 창 배치는 매 실행 초기화하며 `imgui.ini`를 작업 디렉터리에 생성하지 않습니다.

## 검증

```sh
# 디스플레이 없이 컨트롤러/키 입력/스텝/브레이크포인트/파일 왕복 검사
ctest --test-dir ttl_forth_emu/build -R '^native_frontend_controller$' --output-on-failure
# 실제 창/렌더러를 열고 네이티브 FORTH 입력·출력을 확인한 뒤 자동 종료
ttl_forth_emu/build/frontend/sokol_imgui/ttl_forth_sokol_imgui --smoke-test
```

GUI 스모크 테스트는 화면/오디오 장치가 필요하므로 기본 CTest에는 넣지 않습니다.
