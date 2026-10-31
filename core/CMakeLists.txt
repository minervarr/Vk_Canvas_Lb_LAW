# Platform-agnostic canvas/text engine. The physical module directories are the
# dependency boundary: base (values, input, layout), draw (canvas and the
# widget code that emits records), gpu (Vulkan orchestration). Each module is
# its own static target so an unused layer is not linked. `vk_canvas_core`
# stays as the compatibility umbrella so existing consumers keep working.
#
# Core code depends only on platform.hh seams (AssetReader / SurfaceProvider /
# FrameWaker), never on platform SDK headers.

# The font engine submodule builds itself as vk_font_core and owns all the
# FreeType + msdfgen wiring; we just consume it.
set(FONT_ENGINE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../first_party/vulkan_font_engine)
add_subdirectory(${FONT_ENGINE_DIR}/core ${CMAKE_BINARY_DIR}/vk_font_core)

# img_decode_kit is independent of vk_canvas (no Vulkan/vk_canvas include
# anywhere in it) — same relationship as the font engine above. art_texture.cc
# is the thin Vulkan-aware wrapper that turns its decoded bytes into a texture.
set(IMG_DECODE_KIT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../first_party/img_decode_kit)
add_subdirectory(${IMG_DECODE_KIT_DIR}/core ${CMAKE_BINARY_DIR}/img_decode_kit)

# Module directories. Their names are also the include roots propagated below,
# so `#include "canvas.hh"` keeps working without callers naming the layer.
set(VCE_CORE_BASE_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/base)
set(VCE_CORE_INPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/input)
set(VCE_CORE_LAYOUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/layout)
set(VCE_CORE_DRAW_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/draw)
set(VCE_CORE_GPU_DIR   ${CMAKE_CURRENT_SOURCE_DIR}/gpu)

# Shared per-module compile settings. Sources contain UTF-8 comments, so cl
# needs /utf-8; the historical -O2 in Debug is preserved deliberately.
function(vce_canvas_module target)
    target_compile_features(${target} PUBLIC cxx_std_17)
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8)
    else()
        target_compile_options(${target} PRIVATE -O2)
    endif()
endfunction()

# ── base: value types, text data, input, layout ──────────────────────────────
#
# Pure logic. No Vulkan, no font headers, no Canvas. input/ and layout/ are
# separate directories but ship in this target: they are small and every
# consumer of one needs the value types anyway.
add_library(vk_canvas_base STATIC
    base/types.hh
    base/texture.hh
    base/log.hh
    base/animated_float.cc
    input/frame_input.cc
    input/gesture.cc
    layout/layout.cc
    base/text_buffer.cc
    base/undo_redo.cc
)
vce_canvas_module(vk_canvas_base)
target_include_directories(vk_canvas_base PUBLIC
    ${VCE_CORE_BASE_DIR}
    ${VCE_CORE_INPUT_DIR}
    ${VCE_CORE_LAYOUT_DIR}
)
if(ANDROID)
    # log.hh routes to logcat.
    target_link_libraries(vk_canvas_base PUBLIC android log)
endif()

# ── draw: record emitters and the Canvas-based helpers ───────────────────────
#
# Depends on base and the font engine's public types. No Vulkan: the Canvas
# only emits records, which is what makes canvas_records_test CPU-only.
add_library(vk_canvas_draw STATIC
    draw/canvas.cc
    draw/text_util.cc
    draw/widgets.cc
    draw/textarea.cc
    draw/plotview.cc
    draw/pager.cc
    draw/pager_draw.cc
)
vce_canvas_module(vk_canvas_draw)
target_include_directories(vk_canvas_draw PUBLIC
    ${VCE_CORE_DRAW_DIR}
)
# vk_font_core PUBLIC-exposes font.hh/glyphs.hh/msdf.hh + the freetype/msdfgen
# link chain, so linking it is all draw needs for text metrics.
target_link_libraries(vk_canvas_draw PUBLIC vk_canvas_base vk_font_core)

# ── gpu: Vulkan orchestration and the platform seam ──────────────────────────
add_library(vk_canvas_gpu STATIC
    gpu/renderer.cc
    gpu/renderer_api.cc
    gpu/renderer_camera.cc
    gpu/renderer_device.cc
    gpu/renderer_readback.cc
    gpu/renderer_swapchain.cc
    gpu/renderer_text.cc
    gpu/renderer_text_pipeline.cc
    gpu/renderer_shapes.cc
    gpu/overlay.cc
    gpu/image_layer.cc
    gpu/output_target.cc
    gpu/headless.cc
    gpu/art_texture.cc
    gpu/platform.hh
)
vce_canvas_module(vk_canvas_gpu)
target_include_directories(vk_canvas_gpu PUBLIC
    ${VCE_CORE_GPU_DIR}
)
target_link_libraries(vk_canvas_gpu PUBLIC
    vk_canvas_draw
    vk_canvas_base
    vk_font_core
    img_decode_kit
)

if(ANDROID)
    # NDK provides libvulkan directly; log.hh routes to logcat and the camera
    # path imports AHardwareBuffers.
    target_link_libraries(vk_canvas_gpu PUBLIC vulkan)
else()
    find_package(Vulkan REQUIRED)  # headers + loader from $ENV{VULKAN_SDK}
    target_link_libraries(vk_canvas_gpu PUBLIC Vulkan::Vulkan)
endif()

# ── The composite push block, checked before anything compiles ─────────────
#
# renderer.cc's CompositePush and composite_frag.slang's cbuffer PC are written
# to the same bytes and validated by nothing at runtime. They drifted apart
# once and the only symptom was a wrong-looking picture. See
# cmake/check_composite_pc.cmake for what that cost.
add_custom_target(vk_canvas_check_composite_pc ALL
    COMMAND ${CMAKE_COMMAND}
            -DSHADER=${FONT_ENGINE_DIR}/shaders_src/composite_frag.slang
            -DCPP=${VCE_CORE_GPU_DIR}/renderer_impl.hh
            -P ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/check_composite_pc.cmake
    BYPRODUCTS ""
    COMMENT "Checking composite_frag.slang's push block against CompositePush")
add_dependencies(vk_canvas_gpu vk_canvas_check_composite_pc)

# ── Compatibility umbrella ──────────────────────────────────────────────────
#
# Existing consumers link exactly this name. It is an INTERFACE target now, so
# it carries include paths and link dependencies without merging the modules
# back into one archive.
add_library(vk_canvas_core INTERFACE)
target_compile_features(vk_canvas_core INTERFACE cxx_std_17)
target_link_libraries(vk_canvas_core INTERFACE
    vk_canvas_base
    vk_canvas_draw
    vk_canvas_gpu
)

# Pure-logic tests. They compile their module source directly, so they never
# need a Vulkan device, a shader or a window. add_test() is emitted only when
# the parent project enabled CTest (the dev superbuild under dev/ does); the
# executables still build in a plain Debug configure, preserving the existing
# Windows Build.bat behaviour.
if(NOT ANDROID AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(VCE_CORE_INCLUDE_DIRS
        ${VCE_CORE_BASE_DIR}
        ${VCE_CORE_INPUT_DIR}
        ${VCE_CORE_LAYOUT_DIR}
        ${VCE_CORE_DRAW_DIR}
        ${VCE_CORE_GPU_DIR}
    )

    function(vce_add_pure_test name)
        add_executable(${name} tests/${name}.cc ${ARGN})
        target_include_directories(${name} PRIVATE ${VCE_CORE_INCLUDE_DIRS})
        target_compile_features(${name} PRIVATE cxx_std_17)
        if(MSVC)
            target_compile_options(${name} PRIVATE /W3 /utf-8)
        else()
            target_compile_options(${name} PRIVATE -Wall -Wextra)
        endif()
        if(BUILD_TESTING)
            add_test(NAME ${name} COMMAND $<TARGET_FILE:${name}>)
            set_tests_properties(${name} PROPERTIES LABELS unit)
        endif()
    endfunction()

    # text_cover_test pins the two text-edge formulas msdf_frag.slang implements.
    # The shader cannot be executed from here. Header only — no Vulkan, no font.
    vce_add_pure_test(text_cover_test)
    vce_add_pure_test(layout_test layout/layout.cc)
    vce_add_pure_test(animated_float_test base/animated_float.cc)
    vce_add_pure_test(frame_input_test input/frame_input.cc)
    vce_add_pure_test(output_target_test gpu/output_target.cc)
    # output_target.hh includes <vulkan/vulkan.h> for VkFormat/VkColorSpaceKHR;
    # the test itself never calls the Vulkan API.
    target_link_libraries(output_target_test PRIVATE Vulkan::Vulkan)

    vce_add_pure_test(gesture_test input/gesture.cc)
    vce_add_pure_test(text_buffer_test base/text_buffer.cc)
    vce_add_pure_test(undo_redo_test base/undo_redo.cc base/text_buffer.cc)
    # pager.cc is the page model; pager_draw.cc is the Canvas half and stays
    # out of this test on purpose.
    vce_add_pure_test(pager_test draw/pager.cc)
    # Canvas is an emitter; the test inspects the float records directly.
    # canvas.cc references the font engine's text helpers, so link that, not
    # the whole renderer.
    vce_add_pure_test(canvas_records_test draw/canvas.cc)
    target_link_libraries(canvas_records_test PRIVATE vk_font_core)

    # No executable work: the static_asserts are the test. Linking the core
    # target supplies the Vulkan and font-engine include paths.
    vce_add_pure_test(owner_semantics_test)
    target_link_libraries(owner_semantics_test PRIVATE vk_canvas_core)

    # Pixel check of the same formulas, through the real shader and swapchain.
    # Skips (exit 77) without a headless surface, the built shaders, or the face.
    add_executable(text_edge_probe tests/text_edge_probe.cc)
    target_include_directories(text_edge_probe PRIVATE ${VCE_CORE_INCLUDE_DIRS})
    target_compile_features(text_edge_probe PRIVATE cxx_std_17)
    target_compile_definitions(text_edge_probe PRIVATE
        TEXT_EDGE_ASSET_ROOT="${CMAKE_BINARY_DIR}/assets/"
        TEXT_EDGE_FONT="${CMAKE_CURRENT_SOURCE_DIR}/../assets/fonts/newcomputermodern/NewCM10-Regular.otf")
    target_link_libraries(text_edge_probe PRIVATE vk_canvas_core)
    if(MSVC)
        target_compile_options(text_edge_probe PRIVATE /W3 /utf-8)
    else()
        target_compile_options(text_edge_probe PRIVATE -Wall -Wextra)
    endif()

    # Pixel check for Canvas::rectOutline: the border has to follow the rounded
    # corners, not leave a straight line across the old sharp corner. Skips
    # (exit 77) without a headless surface or the built shape shaders.
    add_executable(shape_outline_probe tests/shape_outline_probe.cc)
    target_include_directories(shape_outline_probe PRIVATE ${VCE_CORE_INCLUDE_DIRS})
    target_compile_features(shape_outline_probe PRIVATE cxx_std_17)
    target_compile_definitions(shape_outline_probe PRIVATE
        SHAPE_OUTLINE_ASSET_ROOT="${CMAKE_BINARY_DIR}/assets/")
    target_link_libraries(shape_outline_probe PRIVATE vk_canvas_core)
    if(MSVC)
        target_compile_options(shape_outline_probe PRIVATE /W3 /utf-8)
    else()
        target_compile_options(shape_outline_probe PRIVATE -Wall -Wextra)
    endif()
endif()
