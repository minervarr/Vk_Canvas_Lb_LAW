# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Init submodules (first time)
git submodule update --init --recursive

# Build debug APK
./gradlew assembleDebug

# Build release APK
./gradlew assembleRelease

# Install on connected device
./install.bat

# Clean build artifacts
./gradlew clean
```

**Prerequisites:**
- Android NDK 29.0.14206865 (set `ANDROID_NDK_HOME`)
- CMake 3.22.1+
- FreeType source — lives inside the `vulkan_font_engine` submodule at `first_party/vulkan_font_engine/app/src/main/freetype` (init submodules first)
- Slang compiler (`slangc.exe`) from Vulkan SDK 1.4.341.1 — required for recompiling shaders
- No tests to run; this is a demo app with no test framework

## Shader Compilation

Shaders are written in **Slang** (in `app/src/main/shaders_src/`) and compiled to SPIR-V (into `app/src/main/assets/shaders/`). CMake invokes `slangc.exe` automatically. To recompile manually:

```bash
slangc.exe <shader>.slang -target spirv -o <output>.spv
```

## Architecture

The canvas engine renders 2D content (currently text) on the GPU via Vulkan compute shaders. Font rasterisation is provided by the `vulkan_font_engine` submodule at `first_party/vulkan_font_engine`; its `font.cc` / `glyphs.cc` / `font.hh` are compiled directly into `vk_canvas`.

### Component Map

| File | Role |
|------|------|
| `main.cc` | Android NDK `android_main()` entry; event loop, window lifecycle |
| `app.hh/cc` | Vulkan bootstrap and lifecycle; owns Renderer and Canvas |
| `renderer.hh/cc` | Vulkan device/swapchain setup; draws a Canvas each frame |
| `canvas.hh/cc` | Scene description: list of text draw calls with position + size |
| `font.hh/cc` | (from submodule) FreeType wrapper; converts glyphs to CurveRecords |
| `glyphs.hh/cc` | (from submodule) Hardcoded fallback glyphs when FreeType is unavailable |

### Submodule

`first_party/vulkan_font_engine` — GPU font rendering library. Provides FreeType integration, curve rasterisation shaders, and the CurveRecord format consumed by the canvas renderer.

## Project Configuration

- **App ID**: `io.nava.vkcanvas`
- **Native library**: `vk_canvas` (loaded by `NativeActivity`)
- **Min SDK**: 26 (Android 8.0); **Compile SDK**: 37
- **ABIs**: `arm64-v8a`, `x86_64`
- **C++ standard**: C++17, `-O2`
