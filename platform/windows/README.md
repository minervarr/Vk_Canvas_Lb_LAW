# platform/windows — Win32 backend

Raw `windows.h`, no GLFW/Qt. Self-contained: `Build.bat` → `build_msvc.ps1`
(vswhere → vcvars64 → Ninja → cl) → `build/vk_canvas.exe`, with shaders
compiled to `build/assets/shaders/` next to the exe.

Implements the `core/platform.hh` seams:

- `Win32SurfaceProvider` — reports `VK_KHR_surface` + `VK_KHR_win32_surface`,
  creates via `vkCreateWin32SurfaceKHR` (defines `VK_USE_PLATFORM_WIN32_KHR`
  locally, never in core)
- `FileAssetReader` — reads `assets/` relative to the executable
- `main.cc` — `RegisterClassEx` / `CreateWindowEx` / WndProc message pump;
  Esc quits

Current limitations (next work items):
- Fixed-size window — the renderer doesn't recreate the swapchain on
  resize/`VK_ERROR_OUT_OF_DATE_KHR` yet
- No mouse/keyboard input mapping beyond Esc
- No `FrameWaker` implementation (nothing needs it off-Android yet;
  `PostMessage(WM_NULL)` when it does)
