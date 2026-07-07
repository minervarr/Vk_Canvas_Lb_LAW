cmake_minimum_required(VERSION 3.22.1)
project(vk_canvas_android)

set(NATIVE_APP_GLUE ${ANDROID_NDK}/sources/android/native_app_glue)

set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
set(FONT_ENGINE_DIR ${REPO_ROOT}/first_party/vulkan_font_engine/app/src/main)

# FreeType lives inside the font engine submodule.
# Override with: cmake -DFREETYPE_SOURCE_DIR=/your/path/to/freetype-src
if(NOT DEFINED FREETYPE_SOURCE_DIR)
    set(FREETYPE_SOURCE_DIR
        "${FONT_ENGINE_DIR}/freetype"
        CACHE PATH "Path to FreeType source directory")
endif()

set(FT_DISABLE_ZLIB      ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2     ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG       ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ  ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI    ON CACHE BOOL "" FORCE)
add_subdirectory(${FREETYPE_SOURCE_DIR} ${CMAKE_BINARY_DIR}/freetype)

# msdfgen looks for a Freetype::Freetype target; this FreeType only exports the
# plain `freetype` target, so alias it before pulling msdfgen in.
if(NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype ALIAS freetype)
endif()

# msdfgen (vendored inside the font engine submodule): used by its cpp/msdf.cc.
set(MSDFGEN_BUILD_STANDALONE OFF CACHE BOOL "" FORCE)
set(MSDFGEN_USE_VCPKG OFF CACHE BOOL "" FORCE)
set(MSDFGEN_USE_SKIA OFF CACHE BOOL "" FORCE)
set(MSDFGEN_DISABLE_SVG ON CACHE BOOL "" FORCE)
set(MSDFGEN_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(MSDFGEN_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(${FONT_ENGINE_DIR}/msdfgen ${CMAKE_BINARY_DIR}/msdfgen)

# Slang shader compilation (shared sources at repo root; .spv packaged from assets/)
include(${REPO_ROOT}/cmake/VceShaders.cmake)
vce_compile_slang(compile_shaders
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders
    ${REPO_ROOT}/shaders_src
    composite_vert
    composite_frag
    tiling
    coverage
    overlay_vert
    overlay_frag
)

add_library(vk_canvas SHARED
    cpp/main.cc
    cpp/app.cc
    cpp/renderer.cc
    cpp/overlay.cc
    cpp/canvas.cc
    cpp/textarea.cc
    cpp/widgets.cc
    ${FONT_ENGINE_DIR}/cpp/font.cc
    ${FONT_ENGINE_DIR}/cpp/glyphs.cc
    ${FONT_ENGINE_DIR}/cpp/msdf.cc
    ${NATIVE_APP_GLUE}/android_native_app_glue.c
)

add_dependencies(vk_canvas compile_shaders)

target_include_directories(vk_canvas PRIVATE
    ${NATIVE_APP_GLUE}
    cpp/
    ${FONT_ENGINE_DIR}/cpp
    ${FONT_ENGINE_DIR}/msdfgen
    ${FREETYPE_SOURCE_DIR}/include
)

target_compile_options(vk_canvas PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-std=c++17>
    -O2
)

# msdfgen-core linked explicitly (msdfgen-ext links it only PRIVATE): its PUBLIC
# `MSDFGEN_PUBLIC=` define makes msdfgen/core/base.h skip the generated
# msdfgen-config.h, which only exists under MSDFGEN_INSTALL.
target_link_libraries(vk_canvas
    freetype
    msdfgen::msdfgen-ext
    msdfgen::msdfgen-core
    android
    log
    vulkan
)
