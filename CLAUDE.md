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

# Windows (from repo root; vswhere -> vcvars64 -> Ninja -> cl)
./Build.bat                  # produces build/vk_canvas.exe
```

**Prerequisites:**
- Android NDK 29.0.14206865 (set `ANDROID_NDK_HOME`)
- CMake 3.22.1+
- FreeType and msdfgen sources — vendored inside the `vulkan_font_engine` submodule under `first_party/vulkan_font_engine/app/src/main/` (init submodules first)
- Slang compiler (`slangc`) from the Vulkan SDK — resolved from `$VULKAN_SDK` (override with `-DVCE_SLANGC=...`); required for recompiling shaders
- No tests to run; this is a demo app with no test framework

## Shader Compilation

Shaders are written in **Slang** in the repo-root `shaders_src/` and compiled to SPIR-V into `platform/android/app/src/main/assets/shaders/` (packaged into the APK) or `build/assets/shaders/` (next to the Windows exe). Six shaders: `composite_vert`, `composite_frag`, `tiling`, `coverage`, `overlay_vert`, `overlay_frag`. CMake invokes slangc via `cmake/VceShaders.cmake`. To recompile manually:

```bash
slangc.exe <shader>.slang -target spirv -o <output>.spv
```

## Architecture

One platform-agnostic engine core plus thin per-platform backends:

```
core/                      # vk_canvas_core STATIC lib — no platform SDK includes
  platform.hh              # the seam: AssetReader / SurfaceProvider / FrameWaker + DeviceCaps
  renderer.* overlay.* canvas.* textarea.* widgets.*    (compiled)
  canvas_host.* vulkan_state.* compute_context.* text_editor.*
  text_buffer.* undo_redo.* pager.* plotview.* gesture.*  (WIP — moved, not compiled)
shaders_src/               # shared Slang sources
platform/
  android/                 # self-contained Gradle project: gradlew, build.gradle,
                           #   settings.gradle, install/logcat/screenshot .bat,
                           #   NDK glue + CMake entry, app/ module (manifest + assets)
  windows/                 # Win32 backend (raw windows.h, no GLFW)
  linux/                   # planned Wayland backend (README only)
first_party/vulkan_font_engine/   # submodule (FreeType, msdfgen, font/glyphs/msdf sources)
CMakeLists.txt + Build.bat + build_msvc.ps1   # desktop build entry (root)
```

Rules of the structure:
- **Core never includes platform SDK headers.** Platform needs go through `core/platform.hh`: `AssetReader` (shader/font bytes), `SurfaceProvider` (instance extensions + VkSurfaceKHR creation + extent), `FrameWaker` (wake the render loop). Android implements these in `platform/android/android_platform.{hh,cc}`.
- **Feature use is capability-driven, never platform-hardcoded.** `Renderer` fills a `DeviceCaps` struct at physical-device selection; optional techniques gate on caps so mobile limits never cap PC.
- The Android-only camera path (AHardwareBuffer import + ycbcr conversion) lives in `core/renderer.cc` under `#if defined(__ANDROID__)`.
- `platform/android/CMakeLists.txt` is the Android CMake entry (`platform/android/app/build.gradle` → `externalNativeBuild.cmake.path`); the root `CMakeLists.txt` is the desktop entry. Both `add_subdirectory` `core/`.

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

`first_party/vulkan_font_engine` — GPU font rendering library. Provides FreeType integration, msdfgen, curve rasterisation shaders, and the CurveRecord format consumed by the canvas renderer. Known Phase 2 task: its `msdf.cc/.hh` still use `<android/log.h>` and `AAssetManager` in their API (needs a commit in that repo before desktop builds).

## Project Configuration

- **App ID**: `io.nava.vkcanvas`
- **Native library**: `vk_canvas` (loaded by `NativeActivity`)
- **Min SDK**: 26 (Android 8.0); **Compile SDK**: 37
- **ABIs**: `arm64-v8a`, `x86_64`
- **C++ standard**: C++17, `-O2`
