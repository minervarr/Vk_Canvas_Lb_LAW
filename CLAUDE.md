# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

`vulkan_canvas_engine` (vk_canvas) — a reusable, cross-platform Vulkan canvas/UI engine library: 2D primitives (compute-rasterized curves + analytic-SDF shape quads), MSDF/MTSDF text via the `vulkan_font_engine` submodule, image textures, immediate-mode widgets, and layout/animation/input helpers. Android + Windows backends today; Linux/Wayland planned. Licensed AGPLv3 (see `LICENSE`). Consumed by other apps (e.g. `matrix_player_windows`) — the `platform/` demos here exist to exercise the engine, but `core/` is the product. API-shaping decisions should favor consumers, not the demos.

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
- Unit tests: pure-logic modules have plain `assert()`-based test exes (no framework) in `core/tests/` — `layout_test`, `animated_float_test`, `frame_input_test`. Built by the Windows build (`Build.bat`), then run each from `platform/windows/build/<name>.exe`. Rendering has no automated tests (verified visually). Test sources `#undef NDEBUG` so asserts survive the Release build.

## Shader Compilation

Shaders are written in **Slang** and compiled to SPIR-V into `platform/android/app/src/main/assets/shaders/` (packaged into the APK) or `platform/windows/build/assets/shaders/` (next to the Windows exe), via two `vce_compile_slang()` calls (`cmake/VceShaders.cmake`) per platform CMakeLists: one pointing at `shaders_src/` for vk_canvas's own 6 shaders (`overlay_vert/frag`, `image_vert/frag`, `shape_vert/frag` — see "SDF Shape Pipeline" below), one pointing at `first_party/vulkan_font_engine/shaders_src/` for the shaders vk_canvas shares with the font engine (`composite_vert/frag`, `tiling`, `coverage`, plus `msdf_vert/frag` for consumers that need them — see that repo's own CLAUDE.md). These used to be hand-duplicated in both `shaders_src/` directories; that drifted silently once (a whitespace difference), so vk_canvas now sources them from the font engine's copy instead of maintaining a second one.

**Note:** `platform/windows/CMakeLists.txt`'s own standalone demo build does not currently compile `msdf_vert/frag` (only `composite/tiling/coverage` from the shared set) — its demo doesn't call `Renderer::initMsdf`. A consumer app that wants MSDF text (like `matrix_player_windows`, which lists `msdf_vert`/`msdf_frag` in its own top-level `CMakeLists.txt`) must add those two names to its own `vce_compile_slang()` call.

To recompile a shader manually:

```bash
slangc.exe <shader>.slang -target spirv -o <output>.spv
```

## Architecture

One platform-agnostic engine core plus thin per-platform backends:

```
core/                      # vk_canvas_core STATIC lib — no platform SDK includes
  platform.hh              # the seam: AssetReader / SurfaceProvider / FrameWaker + DeviceCaps
  renderer.* overlay.* canvas.* textarea.* widgets.*    (compiled)
  art_texture.*            # decode-a-file-into-a-texture helper (uses img_decode_kit)
  text_util.*              # truncate/wrap/center helpers on top of Canvas (see below)
  canvas_host.* vulkan_state.* compute_context.* text_editor.* text_buffer.*
  undo_redo.* pager.* plotview.* gesture.* archive.*      (WIP — moved, not compiled)
shaders_src/               # vk_canvas's OWN 6 shaders (overlay/image/shape); the
                           #   ones shared with the font engine live in its own
                           #   shaders_src/ — see "Shader Compilation" above
platform/
  android/                 # self-contained Gradle project: gradlew, build.gradle,
                           #   settings.gradle, install/logcat/screenshot .bat,
                           #   NDK glue + CMake entry, app/ module (manifest + assets)
  windows/                 # self-contained Win32 backend (raw windows.h, no GLFW):
                           #   Build.bat + build_msvc.ps1 + standalone CMakeLists
  linux/                   # planned Wayland backend (README only)
first_party/
  vulkan_font_engine/      # submodule: core/ (vk_font_core lib) + third_party/ (FreeType, msdfgen) + platform/android/ demo
  img_decode_kit/          # submodule-like dep (own core/CMakeLists.txt, no vk_canvas
                           #   include anywhere in it): JPEG (turbojpeg if the consumer
                           #   provides a `turbojpeg-static` target, else stb_image) +
                           #   any other stb_image format, decoded/box-downsampled to
                           #   the caller's target size. art_texture.cc is the thin
                           #   Vulkan-aware wrapper that turns its output into a texture.
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
| `core/renderer.hh/cc` | Vulkan instance/device/swapchain; two frames in flight (see below); draws curve buffer, SDF shape quads, and MSDF text quads each frame |
| `core/overlay.hh/cc` | Tiling + coverage compute rasteriser, composites over the frame. Its screen-size GPU resources (curve/tile/row buffers, output image — the expensive part) are allocated **lazily on first non-empty `uploadCurves()`** — a host that only uses `Canvas::useShapes()` (see below) never allocates them at all |
| `core/canvas.hh/cc` | Immediate-mode scene builder. Two output modes per primitive: default emits 20-float curve records (compute-rasterized); `useShapes()` reroutes rect/segment/polyline/triangle into SDF shape quads instead (see "SDF Shape Pipeline") |
| `core/art_texture.hh/cc` | `createTextureFromImageFile()`: read a file via `AssetReader` → decode+scale via img_decode_kit → upload as a `Renderer` texture, one call. `mips` parameter (default true) — pass false when the texture is drawn at ≥ its decode size (never minified): skips the mip chain, ~33% less VRAM, no per-upload blit pass |
| `core/layout.hh/cc` | Resolution-robust layout math (pure, no Vulkan): `UiScale` (proportional scale — floored, uncapped, plus `cappedFactor` for vertically stacking layouts), `clampScroll`, `dockTop/Bottom/Left/Right` (carve strips off a container Rect in place), `centerIn`, `RowCursor`/`ColumnCursor` (gap-chaining). Compute layout from the surface size every frame with these instead of hardcoding pixels. Geometry sibling of `responsive_text.hh` (which stays the text-size formula) |
| `core/animated_float.hh/cc` | `AnimatedFloat` + `easeLinear`/`easeInOutCubic`: per-frame interpolation primitive (call `update(dt)` each frame, read `value()`). Clamps at the target on overshoot; `set()` restarts from the current value. The renderer deliberately has no animation concept — callers own these |
| `core/frame_input.hh/cc` | `FrameInput`: per-frame edge-detected input state implementing `input.hh`'s `InputSink` (`pointerWentDown`/`pointerWentUp` true only on the transition frame, `wheelDelta` accumulated per frame, `keysWentDown` keycode queue). Call `beginFrame()` once per frame *before* pumping platform events. Saves every consumer hand-rolling event→immediate-mode accumulation |
| `core/text_util.hh/cc` | `truncateToWidth`/`splitTwoLines`/`wrapText` (measure-based, UTF-8-safe, work with any `FontStyle`), `textCenteredStyled` (correct MSDF-measured centering — `Canvas::textCentered` measures the *curve* font and mis-centers MSDF-rendered text), `stripHtmlToPlain` (flatten simple HTML sidecar files to readable paragraphs) |
| `font.hh/cc`, `glyphs.hh/cc`, `msdf.*` | (from submodule) FreeType wrapper / fallback glyphs / MSDF-or-MTSDF atlas — see the font engine's own CLAUDE.md |

### SDF Shape Pipeline (the "MSDF for primitives" fast path)

`Canvas::useShapes(&out)` reroutes `rect()`/`segment()`/`polyline()`/`triangle()` into per-shape quads (14 floats/vert — `Renderer::kShapeFloatsPerVert`) whose fragment shader (`shaders_src/shape_frag.slang`) evaluates the shape's **exact analytic SDF** (rounded-box / capsule / triangle — `shape_frag.slang`'s `kind` field selects which) and converts distance to coverage, normalized by `fwidth(d)` so the antialiasing band is exactly one *screen* pixel under any scale/DPI/transform. This is deliberately NOT an atlas: a rounded rect is a ~10-instruction closed-form distance function, cheaper than a texture fetch, and exact at every corner radius/size combination (an atlas can't represent that continuous parameter space without either a cell explosion or blurry stretching). Reserve MSDF/atlasing for what genuinely benefits from baking: text (finite glyph set, complex outlines) and fixed complex artwork (logos/ornaments — bake into the MSDF atlas as a keyless "glyph", or use a plain image texture via `art_texture.hh`).

When to add a new shape *kind* vs. reach for an atlas instead: if it's a **formula with parameters** (another primitive family), add a `kind` to `shape_frag.slang` (a few lines, no VRAM cost, benefits every consumer at every size). If it's a **fixed picture** with no parameters, it doesn't belong in this pipeline at all — bake it or texture it.

`Canvas::useShapes(nullptr)` (the default) falls back to the original compute-rasterized curve path — needed for rotated UI (shape params are screen-axis-aligned only; rotation isn't supported on this path) and for anything that must go through the winding-fill compute rasterizer.

### Frames in flight

`Renderer` runs two frames in flight (`kFramesInFlight = 2`): separate fences/semaphores and per-frame copies of every CPU-written dynamic buffer (MSDF text VBO, shape VBO), so the CPU can build frame N+1 while the GPU executes frame N. **Exception:** the overlay's compute-rasterizer path (curve buffer, tile/row buffers, output image) is single-buffered — `draw()` detects a non-empty `overlay_curves` argument and serializes that frame against the other slot automatically, so curve-path hosts (Android's demo, any rotated UI) get the old fully-serial behavior for free with no extra code. A host that only ever uses the SDF shape path (empty curve buffer every frame) gets full double-buffered overlap.

### Submodule

`first_party/vulkan_font_engine` — GPU font rendering library, organized like this repo (platform-agnostic `core/` + `platform/android/` demo + root `shaders_src/` + `cmake/VfeShaders.cmake`). Its `core/` builds a `vk_font_core` static library (FreeType integration, msdfgen, fallback glyphs, MSDF atlas reader, `CurveRasterizer` + `MsdfTextRenderer` GPU units) that our `core/CMakeLists.txt` consumes via `add_subdirectory` + link; FreeType/msdfgen are its nested submodules under `third_party/`. The engine-specific shaders and the CurveRecord format live here. The engine owns the `AssetReader` seam definition (`core/asset_reader.hh`), which our `core/platform.hh` includes — asset loading through it is fully cross-platform (APK assets on Android, files on desktop).

### Per-frame consumer pattern

How the pure-logic helpers compose in a host's render loop (the demos and consumer apps all follow this shape):

```
frameInput.beginFrame();          // clear last frame's edges FIRST
pumpPlatformEvents();             // backend feeds InputSink -> FrameInput
anim.update(dtSeconds);           // advance AnimatedFloats
Rect screen{0, 0, w, h};          // recompute layout from the CURRENT size
Rect nav = dockTop(screen, uiScale.scale(64, h));   // no hardcoded pixels
// hit-test with frameInput.pointerWentDown/pointerX/Y against widget geoms,
// then draw via Canvas + widgets:: using the same rects
```

Layout is recomputed every frame from the surface size (cheap, pure math) — never cached against a stale resolution.

## Color policy (why there is no "grayscale mode")

Everything draws with a full `Color {r,g,b,a}` (`canvas.hh`); a grayscale look is app policy — pass equal-channel Colors. Deliberately NOT an engine mode: writing R==G==B saves nothing (the swapchain is 4-channel BGRA/RGBA regardless, so fill-rate/bandwidth are identical), and narrowing the API to a single gray channel would break color-capable consumers for zero performance gain. This was evaluated concretely against the `windows_ui_demo` repo's "grayscale-by-construction" pipeline before being rejected here.

## Committing and pushing

Use the wrapper at the repo root — never plain `git commit`/`git push`:

```bat
git_wrapper.exe save "Short summary"     :: stage-all + commit + push in one step
git_wrapper.exe commit "Short summary"   :: commit only (review, then `git_wrapper push`)
```

It forces author/committer to `nava <nava@noreply.com>`, strips `Co-Authored-By:`/"Generated with" lines from the message, and pushes submodules before the parent (pinned/detached third-party submodules are skipped). See `USAGE_gitWrapper.md`.

## Project Configuration

- **App ID**: `io.nava.vkcanvas`
- **Native library**: `vk_canvas` (loaded by `NativeActivity`)
- **Min SDK**: 26 (Android 8.0); **Compile SDK**: 37
- **ABIs**: `arm64-v8a`, `x86_64`
- **C++ standard**: C++17, `-O2`
