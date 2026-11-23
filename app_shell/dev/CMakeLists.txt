# Dev/test superbuild for app_shell. This is not a consumer project and not
# the library's root CMakeLists: it exists so the four CPU-only tests can be
# configured and run without an application and without the font-derived header
# generator.
#
#   cmake -S dev -B /tmp/opencode/app-shell-dev -G Ninja -DCMAKE_BUILD_TYPE=Debug
#   cmake --build /tmp/opencode/app-shell-dev
#   ctest --test-dir /tmp/opencode/app-shell-dev --output-on-failure
#
# The real per-app value comes from app_shell_generate_min_text_size() and the
# app's shipped fonts. The stub below is deliberately only for tests; it keeps
# the test from depending on a font tool or a consumer build.

cmake_minimum_required(VERSION 3.22)

project(app_shell_dev LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)

set(VCE_BUILD_TESTS ON CACHE BOOL "Build vk_canvas's pure tests in this tree" FORCE)

option(APP_SHELL_DEV_BUILD_FUZZ
    "Build the libFuzzer harness under dev/fuzz (clang only)" OFF)

# The enclosing Vk_Canvas checkout. app_shell/dev is now a subdirectory of the
# framework, so two levels up is the repository root. Kept as an override for a
# consumer that checks the framework out under a different name.
set(VK_CANVAS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../.. CACHE PATH
    "Vk_Canvas source root (the directory containing core/)")
# app_shell no longer consumes a generated min-text-size header; its test
# supplies the legibility floor as a value. ui_min_text_size.gen.h.in stays in
# the tree for apps that still use app_shell_generate_min_text_size().

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/.. app_shell)

# ── Header self-containment ──────────────────────────────────────────────────
#
# One translation unit per public app_shell header. Platform/os headers are
# intentionally excluded: Android needs the NDK and JNI types, and the desktop
# hosts are .cc files. Building `check_headers` is the check.
file(GLOB _app_shell_headers RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../*.hh)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/header_check)
set(_app_shell_header_sources)
foreach(_header ${_app_shell_headers})
    string(MAKE_C_IDENTIFIER "${_header}" _header_id)
    set(_header_src ${CMAKE_CURRENT_BINARY_DIR}/header_check/${_header_id}.cc)
    file(GENERATE OUTPUT ${_header_src}
        CONTENT "#include \"${_header}\"\n")
    list(APPEND _app_shell_header_sources ${_header_src})
endforeach()

add_library(app_shell_header_check OBJECT ${_app_shell_header_sources})
target_link_libraries(app_shell_header_check PRIVATE app_shell)
add_custom_target(check_headers DEPENDS app_shell_header_check)

# Optional libFuzzer harness (clang only); tools/audit.sh runs it for a bounded
# time when enabled. Never part of CTest.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/fuzz fuzz)
