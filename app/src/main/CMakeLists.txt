cmake_minimum_required(VERSION 3.22.1)
project(vk_canvas_android)

set(NATIVE_APP_GLUE ${ANDROID_NDK}/sources/android/native_app_glue)

set(FONT_ENGINE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../first_party/vulkan_font_engine/app/src/main)

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

# Slang shader compilation
set(SLANGC         "C:/VulkanSDK/1.4.341.1/Bin/slangc.exe")
set(SHADER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders_src)
set(SHADER_OUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders)

set(SHADERS
    composite_vert
    composite_frag
    tiling
    coverage
)

foreach(SHADER ${SHADERS})
    add_custom_command(
        OUTPUT  ${SHADER_OUT_DIR}/${SHADER}.spv
        COMMAND ${SLANGC} ${SHADER_SRC_DIR}/${SHADER}.slang
                          -o ${SHADER_OUT_DIR}/${SHADER}.spv
                          -target spirv
        DEPENDS ${SHADER_SRC_DIR}/${SHADER}.slang
        COMMENT "Compiling ${SHADER}.slang -> ${SHADER}.spv"
    )
    list(APPEND SPIRV_OUTPUTS ${SHADER_OUT_DIR}/${SHADER}.spv)
endforeach()

add_custom_target(compile_shaders ALL DEPENDS ${SPIRV_OUTPUTS})

add_library(vk_canvas SHARED
    cpp/main.cc
    cpp/app.cc
    cpp/renderer.cc
    cpp/canvas.cc
    cpp/textarea.cc
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
    ${FREETYPE_SOURCE_DIR}/include
)

target_compile_options(vk_canvas PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-std=c++17>
    -O2
)

target_link_libraries(vk_canvas
    freetype
    android
    log
    vulkan
)
