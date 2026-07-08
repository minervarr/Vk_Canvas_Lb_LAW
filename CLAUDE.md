# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Init submodules (first time)
git submodule update --init --recursive

# Android (Gradle project is self-contained in platform/android/)
cd platform/android
./gradlew assembleDebug      # debug APK
./gradlew assembleRelease    # release APK
./install.bat                # install on connected device
./gradlew clean              # clean build artifacts

# Windows (vswhere -> vcvars64 -> Ninja -> cl)
cd platform/windows
./Build.bat                  # produces platform/windows/build/vk_canvas.exe
```

**Prerequisites:**
- Android NDK 29.0.14206865 (set `ANDROID_NDK_HOME`)
- CMake 3.22.1+
- FreeType and msdfgen sources — vendored inside the `vulkan_font_engine` submodule under `first_party/vulkan_font_engine/third_party/` (init submodules first)
- Slang compiler (`slangc`) from the Vulkan SDK — resolved from `$VULKAN_SDK` (override with `-DVCE_SLANGC=...`); required for recompiling shaders
- No tests to run; this is a demo app with no test framework

## Shader Compilation

Shaders are written in **Slang** in the repo-root `shaders_src/` and compiled to SPIR-V into `platform/android/app/src/main/assets/shaders/` (packaged into the APK) or `platform/windows/build/assets/shaders/` (next to the Windows exe). Six shaders: `composite_vert`, `composite_frag`, `tiling`, `coverage`, `overlay_vert`, `overlay_frag`. CMake invokes slangc via `cmake/VceShaders.cmake`. To recompile manually:

```bash
slangc.exe <shader>.slang -target spirv -o <output>.spv
```

## Architecture

One platform-agnostic engine core plus thin per-platform backends:

```
core/                      # vk_canvas_core STATIC lib — no platform SDK includes
  platform.hh              # the seam: AssetReader / SurfaceProvider / FrameWaker + DeviceCaps
  renderer.* overlay.* canvas.* textarea.* widgets.*    (compiled)
  canvas_host.* vulkan_state.* compute_context.* text_editor.* text_buffer.*
  undo_redo.* pager.* plotview.* gesture.* archive.*      (WIP — moved, not compiled)
shaders_src/               # shared Slang sources
platform/
  android/                 # self-contained Gradle project: gradlew, build.gradle,
                           #   settings.gradle, install/logcat/screenshot .bat,
                           #   NDK glue + CMake entry, app/ module (manifest + assets)
  windows/                 # self-contained Win32 backend (raw windows.h, no GLFW):
                           #   Build.bat + build_msvc.ps1 + standalone CMakeLists
  linux/                   # planned Wayland backend (README only)
first_party/vulkan_font_engine/   # submodule: core/ (vk_font_core lib) + third_party/ (FreeType, msdfgen) + platform/android/ demo
```

Rules of the structure:
- **Core never includes platform SDK headers.** Platform needs go through `core/platform.hh`: `AssetReader` (shader/font bytes), `SurfaceProvider` (instance extensions + VkSurfaceKHR creation + extent), `FrameWaker` (wake the render loop). Android implements these in `platform/android/android_platform.{hh,cc}`.
- **Feature use is capability-driven, never platform-hardcoded.** `Renderer` fills a `DeviceCaps` struct at physical-device selection; optional techniques gate on caps so mobile limits never cap PC.
- The Android-only camera path (AHardwareBuffer import + ycbcr conversion) lives in `core/renderer.cc` under `#if defined(__ANDROID__)`.
- Each platform folder owns its CMake entry: `platform/android/CMakeLists.txt` (reached via `platform/android/app/build.gradle` → `externalNativeBuild.cmake.path`) and `platform/windows/CMakeLists.txt` (standalone project, reached via its `Build.bat`). Both `add_subdirectory` `core/`; there is no root CMakeLists.

### Component Map

| File | Role |
|------|------|
| `platform/android/main.cc` | Android NDK `android_main()` entry |
| `platform/android/app.hh/cc` | ALooper event loop, window lifecycle, seam wiring, test scene |
| `platform/android/android_platform.*` | Android impls of the platform.hh seams |
| `core/renderer.hh/cc` | Vulkan instance/device/swapchain; draws curve buffer each frame |
| `core/overlay.hh/cc` | Tiling + coverage compute rasteriser, composites over the frame |
| `core/canvas.hh/cc` | Immediate-mode scene builder emitting 20-float curve records |
| `font.hh/cc`, `glyphs.hh/cc`, `msdf.*` | (from submodule) FreeType wrapper / fallback glyphs / MSDF atlas |

### Submodule

`first_party/vulkan_font_engine` — GPU font rendering library, organized like this repo (platform-agnostic `core/` + `platform/android/` demo + root `shaders_src/` + `cmake/VfeShaders.cmake`). Its `core/` builds a `vk_font_core` static library (FreeType integration, msdfgen, fallback glyphs, MSDF atlas reader, `CurveRasterizer` + `MsdfTextRenderer` GPU units) that our `core/CMakeLists.txt` consumes via `add_subdirectory` + link; FreeType/msdfgen are its nested submodules under `third_party/`. The engine-specific shaders and the CurveRecord format live here. The engine owns the `AssetReader` seam definition (`core/asset_reader.hh`), which our `core/platform.hh` includes — asset loading through it is fully cross-platform (APK assets on Android, files on desktop).

## Project Configuration

- **App ID**: `io.nava.vkcanvas`
- **Native library**: `vk_canvas` (loaded by `NativeActivity`)
- **Min SDK**: 26 (Android 8.0); **Compile SDK**: 37
- **ABIs**: `arm64-v8a`, `x86_64`
- **C++ standard**: C++17, `-O2`
