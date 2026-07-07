# Desktop entry point (Windows for now; Linux/Wayland later). The Android
# build does NOT go through this file — Gradle points externalNativeBuild at
# platform/android/CMakeLists.txt directly.

cmake_minimum_required(VERSION 3.22.1)
project(vk_canvas_desktop CXX C)

add_subdirectory(core)

if(WIN32)
    add_subdirectory(platform/windows)
endif()
