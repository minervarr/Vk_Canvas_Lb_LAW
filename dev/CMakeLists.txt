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
option(VCE_DEV_BUILD_SHADERS
    "Compile the Slang shaders the GPU probes need" ON)
option(VCE_DEV_SANITIZE
    "Build the dev tree with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(VCE_DEV_BUILD_FUZZ
    "Build the libFuzzer harnesses under dev/fuzz (clang only)" OFF)
option(VCE_DEV_BUILD_SIZE_PROBES
    "Build the feature-isolated link probes used by tools/size_report.sh" OFF)
option(VCE_DEV_LTO
    "Build and link the dev tree with interprocedural optimization" OFF)

if(VCE_DEV_LTO)
    # The module option is defined in core/CMakeLists.txt; set it before that
    # directory is added so its targets (and the probes/tests it defines) are
    # compiled and linked with LTO.
    set(VCE_CANVAS_LTO ON CACHE BOOL "Compile vk_canvas targets with IPO" FORCE)
endif()

if(VCE_DEV_SANITIZE)
    if(MSVC)
        message(FATAL_ERROR "VCE_DEV_SANITIZE is for clang/gcc, not MSVC")
    endif()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# GPU probes and the headless smoke test read <build>/assets/shaders. The font
# engine owns composite/msdf/tiling/coverage; this repository owns overlay,
# image and shape.
if(VCE_DEV_BUILD_SHADERS)
    include(${VK_CANVAS_DIR}/cmake/VceShaders.cmake)
    vce_compile_slang(compile_shared_shaders
        ${CMAKE_BINARY_DIR}/assets/shaders
        ${VK_CANVAS_DIR}/first_party/vulkan_font_engine/shaders_src
        composite_vert
        composite_frag
        msdf_vert
        msdf_frag
    )
    vce_compile_slang_stage(compile_shared_compute_shaders
        ${CMAKE_BINARY_DIR}/assets/shaders
        ${VK_CANVAS_DIR}/first_party/vulkan_font_engine/shaders_src
        compute
        tiling
        coverage
    )
    vce_compile_slang(compile_canvas_shaders
        ${CMAKE_BINARY_DIR}/assets/shaders
        ${VK_CANVAS_DIR}/shaders_src
        overlay_vert
        overlay_frag
        image_vert
        image_frag
        shape_vert
        shape_frag
    )
endif()

add_subdirectory(${VK_CANVAS_DIR}/core core)

# Feature-isolated consumers for the binary-size report. They are not part of
# CTest and are only built when the size script enables them.
if(VCE_DEV_BUILD_SIZE_PROBES)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/size size)
endif()

# The probe executables read the SPIR-V at run time, so build the shaders
# before them (the ALL target already does; this keeps a targeted build honest).
if(VCE_DEV_BUILD_SHADERS)
    foreach(_probe text_edge_probe shape_outline_probe gpu_smoke_test renderer_lifecycle_test)
        if(TARGET ${_probe})
            add_dependencies(${_probe} compile_shared_shaders compile_shared_compute_shaders
                             compile_canvas_shaders)
        endif()
    endforeach()
endif()

if(VCE_DEV_BUILD_PLATFORM)
    add_subdirectory(${VK_CANVAS_DIR}/platform/linux linux)
endif()

if(VCE_DEV_BUILD_CAPTURE AND VCE_DEV_BUILD_PLATFORM)
    add_subdirectory(${VK_CANVAS_DIR}/core/capture capture)
endif()

# ── Header self-containment ──────────────────────────────────────────────────
#
# One translation unit per public engine/UI header, each including only that
# header (by its bare name, the source-compatible form callers use). If a
# header depends on another header it does not include, this fails to compile.
# Building the `check_headers` target is the check; no test executable is
# needed. capture/ is a consumer tool, not part of the engine's public surface.
file(GLOB_RECURSE _core_headers ${VK_CANVAS_DIR}/core/*.hh)
file(GLOB_RECURSE _ui_headers ${VK_CANVAS_DIR}/ui/*.hh)
set(_engine_headers ${_core_headers} ${_ui_headers})
list(FILTER _engine_headers EXCLUDE REGEX "/capture/")
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/header_check)
set(_core_header_sources)
foreach(_header ${_engine_headers})
    get_filename_component(_header_name "${_header}" NAME)
    string(MAKE_C_IDENTIFIER "${_header_name}" _header_id)
    set(_header_src ${CMAKE_CURRENT_BINARY_DIR}/header_check/${_header_id}.cc)
    file(GENERATE OUTPUT ${_header_src}
        CONTENT "#include \"${_header_name}\"\n")
    list(APPEND _core_header_sources ${_header_src})
endforeach()

add_library(vce_core_header_check OBJECT ${_core_header_sources})
target_link_libraries(vce_core_header_check PRIVATE vk_canvas_core)
target_include_directories(vce_core_header_check PRIVATE ${VK_CANVAS_DIR}/ui/demo)
add_custom_target(check_headers DEPENDS vce_core_header_check)

# Optional libFuzzer harnesses (clang only). They are never part of CTest;
# tools/audit.sh runs them for a bounded time when enabled.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/fuzz fuzz)
