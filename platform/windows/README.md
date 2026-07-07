# platform/windows — Win32 backend (planned, Phase 2)

Raw `windows.h`, no GLFW/Qt. Will provide the three `core/platform.hh` seams:

- `Win32SurfaceProvider` — reports `VK_KHR_surface` + `VK_KHR_win32_surface`,
  creates via `vkCreateWin32SurfaceKHR` (defines `VK_USE_PLATFORM_WIN32_KHR`
  locally, never in core)
- `FileAssetReader` — reads `assets/` relative to the executable
- `FrameWaker` — `PostMessage(WM_NULL)` to unblock the message pump

Plus `main.cc` (`RegisterClassEx` / `CreateWindowEx` / WndProc pump),
a `CMakeLists.txt` consuming `core/`, and `CMakePresets.json` (MSVC/clang-cl,
Vulkan headers + loader from `$env:VULKAN_SDK`). Shaders compile via
`cmake/VceShaders.cmake` into `$<TARGET_FILE_DIR>/assets/shaders`.

Prerequisite: a portability commit in the `vulkan_font_engine` submodule —
its `msdf.cc/.hh` still use `<android/log.h>` and `AAssetManager` in their API.
