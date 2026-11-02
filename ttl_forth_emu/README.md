# Teratron 에뮬레이터

Gigatron CPU·VGA·오디오 코어를 기반으로 한 독립 프로젝트입니다. 원본 참고 소스는 변경하지 않습니다.
**macOS만 지원·검증 대상으로 합니다.** 코어는 C11, 펌웨어 생성은 Python 3.8+,
네이티브 GUI는 C++20/Objective-C++/Metal을 사용합니다. 외부 패키지 다운로드는 없습니다.

## 빌드 / 실행

워크스페이스 루트에서:

```sh
cmake -S ttl_forth_emu -B ttl_forth_emu/build -DCMAKE_BUILD_TYPE=Debug
cmake --build ttl_forth_emu/build --parallel
ctest --test-dir ttl_forth_emu/build --output-on-failure
ttl_forth_emu/build/frontend/sokol_imgui/ttl_forth_sokol_imgui
```

GUI 없는 빌드는 configure에 `-DTTL_BUILD_NATIVE_GUI=OFF`를 추가합니다.
CMake가 `build/firmware/`에 `forth.flash`, Python용 `forth.json`, C++용 `forth_abi.h`를 생성합니다.
생성물은 직접 수정하지 않습니다. 네이티브 GUI 옆에는 기본 이미지도 복사됩니다.

```forth
: SQUARE DUP * ;
12 SQUARE .
SAVE
```

결과는 **144**입니다. 해석·컴파일·화면 생성·플래시 저장은 모두 에뮬레이터의 네이티브 CPU에서 실행합니다.

## 저장 규칙

- 데이터 RAM은 **64KiB 고정/뱅크 없음**이고, 플래시는 **512KiB I-RAM만** 저장합니다.
- **명시적인 `SAVE`만** I-RAM을 플래시에 기록합니다. 전원 차단 감지/자동 SAVE는 없습니다.
- SAVE하지 않고 전원을 다시 켜면 새 정의와 RAM 변경은 사라지고 이전 플래시로 부팅합니다.
- **SAVE 도중 전원 차단은 정의하지 않습니다.** 복구·원자성·기존 이미지 보존을 보장하지 않습니다.
- 네이티브 GUI의 정상 종료 시 자동 내보내기는 이미 SAVE된 플래시 바이트의 **호스트 파일 기록**입니다.
  저장하지 않은 I-RAM을 자동으로 플래시에 반영하지 않습니다.

## 선택 실행기

```sh
# 네이티브 펌웨어에 실제 PS/2 프레임으로 입력하는 콘솔
python3 ttl_forth_emu/gui/run.py --eval ': ANSWER 42 ;' --eval SAVE --export machine.flash
python3 ttl_forth_emu/gui/run.py --load machine.flash --eval 'ANSWER .'
# 선택 사항: 브라우저 GUI, 127.0.0.1:8765
python3 ttl_forth_emu/gui/server.py
# 짧은 CPU 진단 데모 (FORTH 아님)
ttl_forth_emu/build/ttl_forth_emu --demo --cycles 20
```

브라우저 GUI는 상태 폴링 때만 실행이 진행하고, 플래시 파일은 Export로 직접 내보냅니다.
저수준 실행기의 `--flash`는 정확히 524288바이트를 요구하며 `--cycles`는 DMA 부팅 이후 기준입니다.
`--export-flash`는 현재 플래시의 파일 복사일 뿐 SAVE가 아닙니다. 기존 128KiB Gigatron ROM은 직접 사용할 수 없습니다.

## 문서

- [에뮬레이션 범위·메모리맵·전원 모델·검증](docs/emulation.md): 코어 동작의 기준 문서.
- [FORTH 펌웨어](firmware/README.md): 워드, 예제, RAM 배치, 컴파일 형식.
- [네이티브 GUI](frontend/sokol_imgui/README.md): 단축키, 디버깅, 이미지 파일 관리.

이 프로젝트는 기능 수준의 에뮬레이터이며 실제 TTL 배선의 전기적/160ns 타이밍 검증을 대신하지 않습니다.
현재 FORTH는 최소 token-threaded 구현이고, 화면 갱신은 협력형입니다. 원저작권·MIT 라이선스는 `LICENSE`에 유지합니다.
