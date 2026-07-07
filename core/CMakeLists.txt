# Platform-agnostic canvas/text engine, built as the vk_canvas_core static
# library. Consumed by platform/*/CMakeLists.txt; core code depends only on
# platform.hh seams (AssetReader / SurfaceProvider / FrameWaker), never on
# platform SDK headers.

set(FONT_ENGINE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../first_party/vulkan_font_engine/app/src/main)

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

add_library(vk_canvas_core STATIC
    renderer.cc
    overlay.cc
    canvas.cc
    textarea.cc
    widgets.cc
    ${FONT_ENGINE_DIR}/cpp/font.cc
    ${FONT_ENGINE_DIR}/cpp/glyphs.cc
    ${FONT_ENGINE_DIR}/cpp/msdf.cc
)
# WIP — moved here but not yet ported to the platform seams, deliberately not
# compiled: canvas_host vulkan_state compute_context text_editor text_buffer
# undo_redo pager plotview gesture archive

target_include_directories(vk_canvas_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${FONT_ENGINE_DIR}/cpp
    ${FONT_ENGINE_DIR}/msdfgen
    ${FREETYPE_SOURCE_DIR}/include
)

target_compile_features(vk_canvas_core PUBLIC cxx_std_17)
if(MSVC)
    # Sources contain UTF-8 comments; cl mis-decodes them without /utf-8.
    target_compile_options(vk_canvas_core PRIVATE /utf-8)
else()
    target_compile_options(vk_canvas_core PRIVATE -O2)
endif()

# msdfgen-core linked explicitly (msdfgen-ext links it only PRIVATE): its PUBLIC
# `MSDFGEN_PUBLIC=` define makes msdfgen/core/base.h skip the generated
# msdfgen-config.h, which only exists under MSDFGEN_INSTALL.
target_link_libraries(vk_canvas_core PUBLIC
    freetype
    msdfgen::msdfgen-ext
    msdfgen::msdfgen-core
)

if(ANDROID)
    # NDK provides libvulkan directly; log.hh routes to logcat and the camera
    # path imports AHardwareBuffers.
    target_link_libraries(vk_canvas_core PUBLIC vulkan android log)
else()
    find_package(Vulkan REQUIRED)  # headers + loader from $ENV{VULKAN_SDK}
    target_link_libraries(vk_canvas_core PUBLIC Vulkan::Vulkan)
endif()
