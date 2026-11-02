set(THIRD_PARTY "${PROJECT_SOURCE_DIR}/../gigatron_emu/3rd_party")
if(NOT APPLE)
    message(FATAL_ERROR "The native frontend supports macOS only. Configure -DTTL_BUILD_NATIVE_GUI=OFF for core-only builds.")
endif()
add_library(ttl_imgui STATIC
    "${THIRD_PARTY}/imgui/imgui.cpp"
    "${THIRD_PARTY}/imgui/imgui_draw.cpp"
    "${THIRD_PARTY}/imgui/imgui_tables.cpp"
    "${THIRD_PARTY}/imgui/imgui_widgets.cpp")
target_include_directories(ttl_imgui PUBLIC "${THIRD_PARTY}/imgui")

add_library(ttl_frontend_controller STATIC controller.cpp)
target_link_libraries(ttl_frontend_controller PUBLIC ttl_machine)
target_include_directories(ttl_frontend_controller PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_BINARY_DIR}/firmware")
add_dependencies(ttl_frontend_controller firmware)

add_executable(ttl_forth_sokol_imgui main.cpp)
target_include_directories(ttl_forth_sokol_imgui PRIVATE "${THIRD_PARTY}/sokol")
target_link_libraries(ttl_forth_sokol_imgui PRIVATE ttl_frontend_controller ttl_imgui)
target_compile_definitions(ttl_forth_sokol_imgui PRIVATE
    TTL_DEFAULT_FLASH="${PROJECT_BINARY_DIR}/firmware/forth.flash")
enable_language(OBJCXX)
set_source_files_properties(main.cpp PROPERTIES LANGUAGE OBJCXX)
set_target_properties(ttl_forth_sokol_imgui PROPERTIES OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED YES)
target_compile_options(ttl_forth_sokol_imgui PRIVATE -fobjc-arc)
target_link_libraries(ttl_forth_sokol_imgui PRIVATE
    "-framework Cocoa" "-framework QuartzCore" "-framework Metal"
    "-framework MetalKit" "-framework AudioToolbox")
add_custom_command(TARGET ttl_forth_sokol_imgui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ttl_forth_sokol_imgui>/firmware"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_BINARY_DIR}/firmware/forth.flash"
        "$<TARGET_FILE_DIR:ttl_forth_sokol_imgui>/firmware/forth.flash")

add_executable(frontend_controller_tests ../../tests/frontend_controller_tests.cpp)
target_link_libraries(frontend_controller_tests PRIVATE ttl_frontend_controller)
add_test(NAME native_frontend_controller COMMAND frontend_controller_tests
    "${PROJECT_BINARY_DIR}/firmware/forth.flash")
