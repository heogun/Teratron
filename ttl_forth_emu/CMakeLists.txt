cmake_minimum_required(VERSION 3.16)
project(ttl_forth_emu C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
add_library(ttl_machine core/gigatron.c core/vga.c core/audio.c
    core/flash.c core/machine.c)
target_include_directories(ttl_machine PUBLIC core)
set_target_properties(ttl_machine PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(ttl_bridge SHARED core/bridge.c)
target_link_libraries(ttl_bridge PRIVATE ttl_machine)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(ttl_machine PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_executable(ttl_forth_emu main.c)
target_link_libraries(ttl_forth_emu PRIVATE ttl_machine)
enable_testing()
add_executable(machine_tests tests/machine_tests.c)
target_link_libraries(machine_tests PRIVATE ttl_machine)
foreach(suite boot cpu flash ps2 checkpoint peripherals ports)
    add_test(NAME ${suite} COMMAND machine_tests ${suite})
endforeach()
add_test(NAME cli_roundtrip COMMAND ${CMAKE_COMMAND}
    -DEMU=$<TARGET_FILE:ttl_forth_emu>
    -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/roundtrip.flash
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_roundtrip.cmake)
add_test(NAME cli_invalid_cycles COMMAND ttl_forth_emu --demo --cycles -1)
set_tests_properties(cli_invalid_cycles PROPERTIES WILL_FAIL TRUE)
find_package(Python3 3.8 COMPONENTS Interpreter REQUIRED)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/firmware/forth.flash
    ${CMAKE_CURRENT_BINARY_DIR}/firmware/forth.json
    ${CMAKE_CURRENT_BINARY_DIR}/firmware/forth_abi.h
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/firmware/build.py
        --output ${CMAKE_CURRENT_BINARY_DIR}/firmware
    DEPENDS firmware/build.py firmware/assembler.py
    VERBATIM)
add_custom_target(firmware ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/firmware/forth.flash
    ${CMAKE_CURRENT_BINARY_DIR}/firmware/forth.json ${CMAKE_CURRENT_BINARY_DIR}/firmware/forth_abi.h)
add_dependencies(ttl_bridge firmware)
add_dependencies(ttl_forth_emu firmware)
add_test(NAME firmware COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_firmware.py
    --library $<TARGET_FILE:ttl_bridge> --firmware ${CMAKE_CURRENT_BINARY_DIR}/firmware)
set_tests_properties(firmware PROPERTIES TIMEOUT 300)
option(TTL_BUILD_NATIVE_GUI "Build the Sokol + Dear ImGui desktop frontend" ON)
if(TTL_BUILD_NATIVE_GUI)
    enable_language(CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    add_subdirectory(frontend/sokol_imgui)
endif()
