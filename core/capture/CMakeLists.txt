# vk_canvas_capture — headless UI-capture runner (dev tooling).
#
# Renders app-supplied Scenarios off-screen via VK_EXT_headless_surface and
# writes a lossless PNG tree. Consumers gate this on a Debug build. On Linux
# it links vk_canvas_wayland for the primary-monitor mode query (no window);
# the consumer must add that subdirectory before this one.

add_library(vk_canvas_capture STATIC capture.cc)

target_include_directories(vk_canvas_capture PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_features(vk_canvas_capture PUBLIC cxx_std_17)
if(MSVC)
    target_compile_options(vk_canvas_capture PRIVATE /utf-8)
else()
    target_compile_options(vk_canvas_capture PRIVATE -O2)
endif()

target_link_libraries(vk_canvas_capture PUBLIC vk_canvas_core)

if(NOT WIN32 AND NOT ANDROID)
    # FileAssetReader + WaylandDisplay (monitor query) live in the Wayland
    # backend; it PUBLIC-exposes platform/linux headers and links the loader.
    target_link_libraries(vk_canvas_capture PUBLIC vk_canvas_wayland)
endif()
