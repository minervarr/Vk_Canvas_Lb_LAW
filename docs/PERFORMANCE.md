# Performance baseline

Status: first baseline, 2026-10-31. These numbers exist so later changes can be
compared against something real. They are **not** budgets yet; budgets are set
in Phase 4 after the renderer split and the benchmark harness exist.

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

## GPU/frame timings

Not measured in this baseline. The probes are built, but the dev tree has no
compiled SPIR-V assets:

```
text_edge_probe       exit 77: shaders missing under <build>/assets/
shape_outline_probe   exit 77: shape shader missing under <build>/assets/
```

To measure later:

1. Compile the shaders into the build tree (the Windows backend does this via
   `VceShaders.cmake`; the Linux dev tree needs the same step or a prebuilt
   `assets/shaders` directory).
2. Ensure `VK_EXT_headless_surface` is available (it is on this machine) and a
   GPU/ICD is selected.
3. Run the probes and a benchmark scene with `FrameStats` (Phase 1) for CPU
   build/submit and GPU timestamps per pass.
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
