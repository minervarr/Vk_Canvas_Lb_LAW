# Dev superbuild for vk_canvas. This is NOT the library's root CMakeLists:
# there is deliberately no root entry point. Consumers add_subdirectory() core/
# and one platform backend themselves. This tree exists so a developer (and CI)
# can configure, build and run every test from one place.
#
#   cmake -S dev -B /tmp/opencode/vk-dev -G Ninja -DCMAKE_BUILD_TYPE=Debug
#   cmake --build /tmp/opencode/vk-dev
#   ctest --test-dir /tmp/opencode/vk-dev --output-on-failure
#
# Unit tests need no GPU. The probes and capture tool need a Vulkan device and
# built shaders; they are built when the backend/capture options are on and
# skip themselves (exit 77) when the environment is missing.

cmake_minimum_required(VERSION 3.22)

project(vk_canvas_dev LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)

set(VK_CANVAS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/.. CACHE PATH
    "Vk_Canvas source root (the directory containing core/)")

option(VCE_DEV_BUILD_PLATFORM
    "Build the Linux/Wayland backend" ON)
option(VCE_DEV_BUILD_CAPTURE
    "Build the headless capture tool (needs the platform backend)" ON)

add_subdirectory(${VK_CANVAS_DIR}/core core)

if(VCE_DEV_BUILD_PLATFORM)
    add_subdirectory(${VK_CANVAS_DIR}/platform/linux linux)
endif()

if(VCE_DEV_BUILD_CAPTURE AND VCE_DEV_BUILD_PLATFORM)
    add_subdirectory(${VK_CANVAS_DIR}/core/capture capture)
endif()

# ── Header self-containment ──────────────────────────────────────────────────
#
# One translation unit per public engine header, each including only that
# header (by its bare name, the source-compatible form callers use). If a
# header depends on another header it does not include, this fails to compile.
# Building the `check_headers` target is the check; no test executable is
# needed. capture/ is a consumer tool, not part of the engine's public surface.
file(GLOB_RECURSE _core_headers RELATIVE ${VK_CANVAS_DIR}/core
    ${VK_CANVAS_DIR}/core/*.hh)
list(FILTER _core_headers EXCLUDE REGEX "^capture/")
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/header_check)
set(_core_header_sources)
foreach(_header ${_core_headers})
    get_filename_component(_header_name "${_header}" NAME)
    string(MAKE_C_IDENTIFIER "${_header}" _header_id)
    set(_header_src ${CMAKE_CURRENT_BINARY_DIR}/header_check/${_header_id}.cc)
    file(GENERATE OUTPUT ${_header_src}
        CONTENT "#include \"${_header_name}\"\n")
    list(APPEND _core_header_sources ${_header_src})
endforeach()

add_library(vce_core_header_check OBJECT ${_core_header_sources})
target_link_libraries(vce_core_header_check PRIVATE vk_canvas_core)
add_custom_target(check_headers DEPENDS vce_core_header_check)
