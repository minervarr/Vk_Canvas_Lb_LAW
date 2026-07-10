# img_decode_kit — platform/renderer-agnostic image decode+scale library.
#
# Given raw file bytes and a target display size, decodes JPEG (via
# turbojpeg's real scaled decompression, when available) or other formats
# (via stb_image + a box-filter downsample) to RGBA8 — never more resolution
# than the target needs, never more than the source has. No Vulkan, no
# windowing, no vk_canvas dependency: reusable by any renderer/app, the same
# way first_party/vulkan_font_engine's core/ is independent of vk_canvas.
#
# turbojpeg is an OPTIONAL dependency, not required: this library must build
# standalone for any consumer, including vk_canvas's own platform/windows and
# platform/android demos, which don't vendor libjpeg-turbo at all (only the
# downstream matrix_player_windows app does, via its own ExternalProject_Add
# — libjpeg-turbo's CMakeLists.txt refuses plain add_subdirectory() unless
# it's the top-level project, so building it is left to whichever app wants
# it). If the consuming build already defines a `turbojpeg-static` target
# (with headers reachable via INTERFACE_INCLUDE_DIRECTORIES) before
# add_subdirectory()'ing this, it's used for the JPEG path; otherwise
# stb_image's own (slower, lower-quality) JPEG decoder covers JPEG too, same
# as it already does for every other format.

add_library(img_decode_kit STATIC
    img_decode.cc
)

target_include_directories(img_decode_kit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_features(img_decode_kit PUBLIC cxx_std_17)
if(MSVC)
    target_compile_options(img_decode_kit PRIVATE /utf-8)
else()
    target_compile_options(img_decode_kit PRIVATE -O2)
endif()

if(TARGET turbojpeg-static)
    target_link_libraries(img_decode_kit PUBLIC turbojpeg-static)
    target_compile_definitions(img_decode_kit PRIVATE IMG_DECODE_HAVE_TURBOJPEG=1)
else()
    target_compile_definitions(img_decode_kit PRIVATE IMG_DECODE_HAVE_TURBOJPEG=0)
endif()
