# Performance baseline

Status: second baseline after Phase 1, 2026-10-31. The Phase 0 numbers remain in
the tables below as the pre-split reference; the "Phase 1 re-baseline" section
records the post-split numbers. These are **not** budgets yet; budgets are set
in Phase 4 after the benchmark harness exists.

## Reference machine

| | |
|---|---|
| Date | 2026-10-31 |
| OS | Linux 7.2.11-arch1-1 x86_64 |
| CPU | Intel Core i7-11800H @ 2.30 GHz, 16 threads |
| GPU | Intel TigerLake-H UHD Graphics + NVIDIA RTX 3050 Mobile (GA107M) |
| Vulkan ICDs present | `intel_icd`, `intel_hasvk_icd`, `nouveau_icd` |
| Vulkan loader | 1.4.363 |
| Compiler | GCC 16.2.1 20260810 |
| CMake / Ninja | 4.4.4 / 1.13.2 |
| Build flags | Debug: `-g -O2` for the core target; Release: CMake Release |
| Build trees | `/tmp/opencode/vk-baseline-{debug,release}`, `/tmp/opencode/app-shell-release` |

This is a laptop with hybrid graphics. Treat absolute numbers as a local
reference, not a hardware-independent claim. Record the machine with every
future baseline.

## Build and test

| Measurement | Result |
|---|---|
| Vk dev configure (Debug) | 0.49 s |
| Vk dev build (Debug, cold) | 10.6 s |
| Vk unit tests (11, Debug) | 0.02 s total |
| App_shell dev configure (Release) | 1.26 s |
| App_shell build (Release, cold) | 4.5 s |
| App_shell unit tests (4, Debug) | 0.00 s total |

Commands:

```bash
cmake -S Vk_Canvas_Lb_LAW/dev -B /tmp/opencode/vk-baseline-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/opencode/vk-baseline-debug
ctest --test-dir /tmp/opencode/vk-baseline-debug --output-on-failure
```

## Static library and minimal-link size

| Artifact | Size |
|---|---|
| `libvk_canvas_core.a`, Debug | 4,866,844 bytes |
| `libvk_canvas_core.a`, Release | 386,596 bytes |
| — Release `.text` / `.data` / `.bss` | 185,709 / 296 / 0 bytes |
| `libapp_shell.a`, Release | 22,460 bytes (text 8,506) |
| Minimal layout-only executable, stripped | 14,472 bytes |
| Minimal Canvas + font executable, stripped | 952,888 bytes |

The two minimal executables answer the "unused code" question concretely:

- The layout-only binary references `dockTop`, so the linker pulls only
  `layout.o` from the archive: ~14 KB including startup.
- The Canvas binary pulls `canvas.o`, which references the font engine's text
  helpers, so the font engine comes with it: ~953 KB stripped.

This is the object-file granularity effect described in the roadmap, not a
runtime cost. Splitting `renderer.cc` and `widgets.cc` and enabling function
sections/GC will make the archive granularity finer. Re-measure after Phase 1
and Phase 4.

## Phase 1 re-baseline (2026-10-31)

The umbrella is now an INTERFACE target, so there is no single
`libvk_canvas_core.a`. The release archives are measured per module.

| Measurement | Result |
|---|---|
| Dev configure, Release, no platform/capture | 0.39 s |
| Dev build, Release cold, no platform/capture | 9.3 s |
| `libvk_canvas_base.a`, Release | 42,202 bytes (text 16,810 / data 128) |
| `libvk_canvas_draw.a`, Release | 162,410 bytes (text 89,278 / data 16) |
| `libvk_canvas_gpu.a`, Release | 217,866 bytes (text 84,227 / data 200) |
| Module archives, total | 422,478 bytes (text 190,315 / data 344) |
| Minimal layout-only executable, stripped | 14,472 bytes |
| Minimal Canvas + font executable, stripped | 952,888 bytes |
| Full CTest run, Debug (14 tests) | 0.24 s; 11 unit + 3 gpu, 1 skipped |

The minimal binaries are unchanged from Phase 0, which is the useful result:
splitting the renderer did not change what a small consumer pays. The module
archive total is ~9% larger than the old single archive for the same code; that
is archive bookkeeping, not runtime cost, and Phase 4 measures the linked
result.

Commands:

```bash
cmake -S Vk_Canvas_Lb_LAW/dev -B /tmp/opencode/vk-release -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DVCE_DEV_BUILD_PLATFORM=OFF \
      -DVCE_DEV_BUILD_CAPTURE=OFF
cmake --build /tmp/opencode/vk-release
# minimal layout consumer
g++ -std=c++17 -O2 -Icore/layout -Icore/base <main.cc> \
    /tmp/opencode/vk-release/core/libvk_canvas_base.a -o min_layout
```


## GPU/frame timings

Measured after Phase 1: the dev tree compiles the SPIR-V assets (see
`VCE_DEV_BUILD_SHADERS`), `VK_EXT_headless_surface` is available, and the
selected device is the Intel UHD iGPU.

| Test | Result |
|---|---|
| `shape_outline_probe` | pass, 0.07 s wall (includes Renderer init) |
| `gpu_smoke_test` | pass, 5 frames in 0.07 s; last frame CPU total 0.033 ms, fence wait 0.001 ms, record 0.001 ms |
| `text_edge_probe` | skip (77): no font at `assets/fonts/newcomputermodern/NewCM10-Regular.otf` |

`gpu_smoke_test` covers overlay curves, shapes, images, clipping, rotation and
FrameStats. MSDF runs only when a face is supplied. These are smoke numbers,
not a frame-rate claim: the frames are tiny (192x128) and the scene is one
primitive per frame.

The shader modules still produce validation warnings
(`VUID-VkShaderModuleCreateInfo-pCode-08737/08740`) because the installed
`slangc` emits a newer SPIR-V version than the 1.1 instance advertises. The
draws are correct on this driver, but "validation-clean" is not yet true; that
is Phase 3 work.

To measure a real scene later:

1. Build with `-DVCE_DEV_BUILD_SHADERS=ON` (default) and a reachable `slangc`.
2. Run with `-DVCE_VK_VALIDATION=ON` while bringing up a path; expect the
   shader-module warnings above until the SPIR-V target is pinned.
3. Add a benchmark scene (shape count, glyph count, images, resolution) and
   use `Renderer::lastFrameStats()` plus GPU timestamps per pass.
4. Record the GPU/ICD used. Driver choice changes the result; never compare
   timings across ICDs silently.

## Allocation and steady-state goals

Not measured yet. The intended target is zero heap allocations in the
steady-state UI frame build (`Canvas` + widgets) after warm-up, and stable RSS
over a long run. A counting-`operator new` test and a soak test are Phase 3/4
work.

## Provisional targets (to confirm after Phase 4)

- 60 fps at 1080p with 2,000 SDF shapes + 5,000 glyphs on integrated graphics.
- CPU frame build under 1 ms for a typical screen.
- Zero steady-state heap allocations in the UI frame build.
- Cold start to first frame under 100 ms on desktop (excluding asset decode).
- Memory stable over 10,000 frames; no Vulkan object leaks at teardown.
- Minimal UI binary under 1 MB stripped after module pruning (excluding fonts
  and shaders) — the current Canvas+font floor is already ~953 KB, so the font
  engine is the binding constraint and must be measured separately.
- Zero validation-layer errors in Debug; zero unchecked Vulkan calls in
  Release.

## What to record for every future measurement

- Commit hash and build type.
- Machine/CPU/GPU/ICD and driver version.
- Compiler and flags.
- Scene: shape count, glyph count, images, overlay-vs-shape path, resolution.
- CPU build, CPU submit, GPU per-pass, frame interval, RSS/VRAM, binary size.
- Whether the run was headless or on a real surface.
