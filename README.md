# Teratron

Gigatron 기반의 FORTH용 TTL 컴퓨터 설계와 에뮬레이터입니다.
데이터 RAM은 **64KiB 고정/뱅크 없음**이며, **512KiB 플래시에는 I-RAM만** 저장합니다.

## 에뮬레이터 실행

CMake, C11, Python 3.8+가 필요합니다. 네이티브 GUI는 C++20도 사용합니다.

```sh
cmake -S ttl_forth_emu -B ttl_forth_emu/build
cmake --build ttl_forth_emu/build --parallel
ctest --test-dir ttl_forth_emu/build --output-on-failure
ttl_forth_emu/build/frontend/sokol_imgui/ttl_forth_sokol_imgui
```

macOS에서 Metal/Sokol/ImGui 창 실행을 검증했습니다. 화면이 없는 환경은
`-DTTL_BUILD_NATIVE_GUI=OFF`로 빌드할 수 있습니다.
상세 사용법은 [`ttl_forth_emu/README.md`](ttl_forth_emu/README.md)에 있습니다.

```forth
: SQUARE DUP * ;
12 SQUARE .
SAVE
```

FORTH 해석/컴파일, PS/2 입력, 화면 생성과 플래시 저장은 에뮬레이터의 네이티브 CPU에서 실행합니다.
현재는 최소 token-threaded FORTH이며, 완전한 ANS FORTH나 연속 VGA 타이밍을 보장하는 실시간 펌웨어는 아닙니다.
플래시 저장 전체가 끝날 때까지 전원이 유지되어야 합니다. [펌웨어 구조와 제약](ttl_forth_emu/firmware/README.md)을 참고하세요.

## 자료 구분

- [`hardware_specification.md`](hardware_specification.md): 새 컴퓨터의 설계 요구사항.
- `ttl_forth_emu/`: 새 머신의 에뮬레이터, 펌웨어, 네이티브/브라우저 GUI와 테스트.
- `gigatron_emu/`, `gigatron_rom/`: 기반 Gigatron 에뮬레이터와 ROM/도구 참고 소스.
- `Gigatron Rev. 2012 OSH/`, `datasheet/`, PDF 자료: 원본 회로/PCB 및 부품 참고 자료.

구성 요소별 라이선스와 원저작권 표기는 각 디렉터리의 LICENSE/파일에 따릅니다.
