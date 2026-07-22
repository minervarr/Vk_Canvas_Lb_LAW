# Platform-agnostic canvas/text engine, built as the vk_canvas_core static
# library. Consumed by platform/*/CMakeLists.txt; core code depends only on
# platform.hh seams (AssetReader / SurfaceProvider / FrameWaker), never on
# platform SDK headers.

# The font engine submodule builds itself as vk_font_core and owns all the
# FreeType + msdfgen wiring; we just consume it.
set(FONT_ENGINE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../first_party/vulkan_font_engine)
add_subdirectory(${FONT_ENGINE_DIR}/core ${CMAKE_BINARY_DIR}/vk_font_core)

# img_decode_kit is independent of vk_canvas (no Vulkan/vk_canvas include
# anywhere in it) — same relationship as the font engine above. art_texture.cc
# is the thin Vulkan-aware wrapper that turns its decoded bytes into a texture.
set(IMG_DECODE_KIT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../first_party/img_decode_kit)
add_subdirectory(${IMG_DECODE_KIT_DIR}/core ${CMAKE_BINARY_DIR}/img_decode_kit)

add_library(vk_canvas_core STATIC
    renderer.cc
    headless.cc
    overlay.cc
    image_layer.cc
    canvas.cc
    textarea.cc
    widgets.cc
    art_texture.cc
    text_util.cc
    layout.cc
    animated_float.cc
    frame_input.cc
)
# WIP — moved here but not yet ported to the platform seams, deliberately not
# compiled: canvas_host vulkan_state compute_context text_editor text_buffer
# undo_redo pager plotview gesture archive

target_include_directories(vk_canvas_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(vk_canvas_core PUBLIC cxx_std_17)
if(MSVC)
    # Sources contain UTF-8 comments; cl mis-decodes them without /utf-8.
    target_compile_options(vk_canvas_core PRIVATE /utf-8)
else()
    target_compile_options(vk_canvas_core PRIVATE -O2)
endif()

# vk_font_core PUBLIC-exposes font.hh/glyphs.hh/msdf.hh + the freetype/msdfgen
# link chain, so linking it is all core needs for text rendering.
target_link_libraries(vk_canvas_core PUBLIC vk_font_core)

# img_decode_kit PUBLIC-exposes img_decode.hh + the turbojpeg link chain.
target_link_libraries(vk_canvas_core PUBLIC img_decode_kit)

if(ANDROID)
    # NDK provides libvulkan directly; log.hh routes to logcat and the camera
    # path imports AHardwareBuffers.
    target_link_libraries(vk_canvas_core PUBLIC vulkan android log)
else()
    find_package(Vulkan REQUIRED)  # headers + loader from $ENV{VULKAN_SDK}
    target_link_libraries(vk_canvas_core PUBLIC Vulkan::Vulkan)
endif()
