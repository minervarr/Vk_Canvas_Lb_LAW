cmake_minimum_required(VERSION 3.22.1)
project(vk_canvas_android)

set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

add_subdirectory(${REPO_ROOT}/core ${CMAKE_BINARY_DIR}/core)

# Slang shader compilation (shared sources at repo root; compiled .spv land in
# the Gradle module's assets/ so AGP packages them into the APK).
# composite/tiling/coverage are canonically owned by the font engine (same
# bytes vk_canvas's own copies used to hand-duplicate — see its shaders_src
# comment); overlay/image are genuinely vk_canvas's own.
include(${REPO_ROOT}/cmake/VceShaders.cmake)
vce_compile_slang(compile_shared_shaders
    ${CMAKE_CURRENT_SOURCE_DIR}/app/src/main/assets/shaders
    ${REPO_ROOT}/first_party/vulkan_font_engine/shaders_src
    composite_vert
    composite_frag
    tiling
    coverage
)
vce_compile_slang(compile_shaders
    ${CMAKE_CURRENT_SOURCE_DIR}/app/src/main/assets/shaders
    ${REPO_ROOT}/shaders_src
    overlay_vert
    overlay_frag
    image_vert
    image_frag
    shape_vert
    shape_frag
)

set(NATIVE_APP_GLUE ${ANDROID_NDK}/sources/android/native_app_glue)

add_library(vk_canvas SHARED
    main.cc
    app.cc
    android_platform.cc
    ${NATIVE_APP_GLUE}/android_native_app_glue.c
)
# WIP Android glue, not yet wired: input_handler orientation fullscreen jni_util

add_dependencies(vk_canvas compile_shared_shaders compile_shaders)

target_include_directories(vk_canvas PRIVATE
    ${NATIVE_APP_GLUE}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_options(vk_canvas PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-std=c++17>
    -O2
)

target_link_libraries(vk_canvas
    vk_canvas_core
    android
    log
    vulkan
)

# Keep the glue's NativeActivity entry point alive against linker GC.
target_link_options(vk_canvas PRIVATE -u ANativeActivity_onCreate)
