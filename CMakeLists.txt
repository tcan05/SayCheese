cmake_minimum_required(VERSION 3.20)

project(SayCheese VERSION 1.0.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "SayCheese targets Windows 10/11 (x64).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static CRT (/MT in Release, /MTd in Debug) for zero VC++ Redistributable dependency.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(saycheese_core STATIC
    src/platform/dpi_monitor.cpp
    src/platform/com_debug.cpp
    src/tools/geometry.cpp
    src/tools/command_stack.cpp
    src/tools/tool_controller.cpp
    src/ui/toolbar_layout.cpp
)

target_include_directories(saycheese_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/resources
)

target_compile_definitions(saycheese_core PUBLIC
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    NTDDI_VERSION=NTDDI_WIN10_RS5
    _WIN32_WINNT=0x0A00
)

if(MSVC)
    target_compile_options(saycheese_core PUBLIC
        /W4
        /permissive-
        /utf-8
        /Zc:__cplusplus
        $<$<CONFIG:Release>:/O2 /GL>
    )
    target_link_options(saycheese_core INTERFACE
        $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>
    )
endif()

target_link_libraries(saycheese_core PUBLIC
    d3d11
    dxgi
    d2d1
    dwrite
    windowscodecs
    dwmapi
    shell32
    comdlg32
    user32
    gdi32
    ole32
    dxguid
    shcore
    psapi
)

enable_testing()

function(saycheese_add_unit_test target_name source_file)
    add_executable(${target_name} ${source_file})
    target_link_libraries(${target_name} PRIVATE saycheese_core)
    target_include_directories(${target_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME ${target_name} COMMAND ${target_name})
endfunction()

saycheese_add_unit_test(test_platform tests/test_platform.cpp)
saycheese_add_unit_test(test_geometry tests/test_geometry.cpp)
saycheese_add_unit_test(test_command_stack tests/test_command_stack.cpp)
saycheese_add_unit_test(test_tool_controller tests/test_tool_controller.cpp)
