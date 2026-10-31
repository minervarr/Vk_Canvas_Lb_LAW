# Reliability

Status: canonical, 2026-10-31 (Phase 3). This file records the ownership,
failure, recovery and threading contracts the renderer is built to, and the
checks that prove them.

## Checked Vulkan calls

Every Vulkan call with a `VkResult` goes through one of three policies from
`core/gpu/vk_check.hh` (rationale and rejected alternatives:
[ADR 0003](decisions/0003-vulkan-error-and-recovery-policy.md)):

| Macro | On failure | Use for |
|---|---|---|
| `VCE_VK_CHECK(expr)` | log + `abort()` | invariants and teardown, where continuing would run a half-built object |
| `VCE_VK_CHECK_THROW(expr)` | log + `std::runtime_error` | constructors and init paths the caller can unwind |
| `VCE_VK_CHECK_OR(expr, action)` | log + `action` (usually `return false`) | runtime allocation/upload paths that degrade to a smaller frame |

Calls whose result is part of the swapchain protocol are **not** wrapped:
`vkAcquireNextImageKHR` and `vkQueuePresentKHR` are classified with
`vce_vk_classify()` into `Success`, `Suboptimal`, `OutOfDate`, `SurfaceLost`,
`DeviceLost` or `Error`, and handled in the matrix below. `vkGetFenceStatus`
is a query (`VK_NOT_READY` is normal), not an error.

`docs/TESTING.md` records the checks; `vk_result_test` pins the classifier.

## Ownership and teardown

- `Renderer`, `OverlayRasterizer`, `ImageLayer` own Vulkan handles. Copy
  construction and assignment are deleted; `owner_semantics_test` fails the
  build if that changes.
- `Renderer::Impl` is a pimpl. It has exactly one destructor path (`cleanup()`),
  also used by the constructor's catch block, so a throw after
  `vkCreateInstance` destroys everything created before it. There is no
  half-initialized `Renderer` in circulation.
- Teardown order is the reverse of creation: device idle, subsystem cleanup
  (overlay, image layer, atlas, shapes), sync objects, command pool,
  swapchain resources, render passes, device, surface, instance.
- Every destroy is null-checked. `destroy_swapchain_resources()` also resets
  `last_drawn_image_index_`, because that index belongs to the swapchain being
  destroyed (the capture path reuses it while present is disabled).
- Memory type lookup returns `UINT32_MAX` when nothing matches. Index 0 is a
  real type; using it as a "not found" value was a silent invalid bind and is
  gone.
- Shader-load failure is reported: `OverlayRasterizer::init()` and
  `ImageLayer::init()` return `false` and the `Renderer` constructor throws.
  A renderer either draws or fails loudly; it does not silently draw nothing.

## Recovery matrix

| Event | Detected by | Renderer action | Host action |
|---|---|---|---|
| Resize (gradual) | `VK_SUBOPTIMAL_KHR` on acquire/present | recreate swapchain | none |
| Resize (jump) | `notifyResized()` | recreate swapchain immediately | call it before the next frame |
| Minimize (0x0) | `SurfaceProvider::extent()` | no-op; keep last frame | none |
| Swapchain out of date | acquire/present | recreate in the same `draw()`, retry acquire once | none |
| Surface lost | acquire/present | `surfaceLost()` = true; `draw()` stops | `recreate_surface()` once the window exists |
| Surface regained, incompatible format | `recreate_surface()` | returns `false`, leaves `surfaceLost()` set | destroy and rebuild the `Renderer` |
| Device lost | wait/reset/submit/acquire/present | `deviceLost()` = true; `draw()` stops | destroy and rebuild the `Renderer` |
| Allocation/upload failure | `VCE_VK_CHECK_OR` | log, skip the draw or shrink it | none |

`recreate_surface()` keeps the instance, device, pipelines, glyph atlas and
uploaded textures; only the surface, swapchain, framebuffers and command
buffers are rebuilt. It refuses to run after device loss.

Device loss is not injectable from a test. The policy is covered at the
classifier level (`vk_result_test`) and the code paths are compiled and
exercised for surface loss/minimize; a real `VK_ERROR_DEVICE_LOST` needs
`VK_EXT_device_fault` or hardware removal and is verified manually.

## Thread contract

`Renderer` is single-threaded: construction, `draw()`, `initMsdf()`,
`create_texture()`, `destroy_texture()`, `readbackLastFrame()`,
`notifyResized()`, `recreate_surface()` and destruction must all happen on the
thread that owns the render loop. The command pool, queue and swapchain are not
guarded.

The deliberate cross-thread entry points are:

- `update_external_frame()` / `update_camera_frame()`: take the producer's
  mutex around the pending handle and release callback. Safe to call from a
  decode/camera thread.
- `set_frame_waker()`: publishes a pointer; call it before the producer thread
  starts.
- `consume_frame_ready()`: atomic exchange; safe from the render loop.
- `set_external_colour()`, `set_external_rotation()`,
  `set_external_visible_size()`, `set_external_transfer()`,
  `set_external_pixel_aspect()`: configuration only, before the first frame
  that consumes the external image. `set_external_colour()` is ignored loudly
  after the conversion object exists.

`SurfaceProvider::extent()` is called from the render thread (`draw()`,
`notifyResized()`, `recreate_surface()`). `SurfaceProvider::create()` is called
during construction and in `recreate_surface()`, always on that same thread.
`AssetReader::read()` is called during construction and texture/glyph upload;
implementations may be called from the render thread only.

## Platform contract

- `SurfaceProvider` owns the native window; the renderer never sees it. A 0x0
  extent means "no drawable right now", not an error. After the platform
  destroys and recreates the window, call `recreate_surface()`; do not keep the
  old surface handle.
- `FrameWaker::wake()` must be safe to call from any thread and must not block.
- Assets are addressed by relative paths (`shaders/foo.spv`); the provider may
  resolve them from an APK, the executable directory or memory.
- The engine asks for a Vulkan 1.1 instance/device. Shaders are compiled to
  SPIR-V 1.3 (Slang → GLSL → glslang) because Slang's direct SPIR-V emission
  targets 1.5, which a 1.1 instance rejects. The device enables
  `shaderDrawParameters` when supported and `samplerAnisotropy`,
  `shaderStorageImageReadWithoutFormat` and
  `shaderStorageImageWriteWithoutFormat` only when the device reports them.
- One output target per process: `resolve_output_target()` runs once, before
  the first swapchain, because the render pass and every pipeline bake it in.
  Changing HDR mode at runtime requires destroying and rebuilding the renderer.

## Checks

```bash
Vk_Canvas_Lb_LAW/tools/audit.sh
```

runs, in order:

1. dependency and source inventory (`tools/check_deps.py`)
2. Debug build + header self-containment + all CTest entries
3. validation-layer build (`VCE_VK_VALIDATION=ON`), every GPU binary, failing on
   any `VUID-` or `Validation Error`
4. ASan+UBSan build (`VCE_DEV_SANITIZE=ON`), full CTest with leak detection on
5. soak: `gpu_smoke_test` and `renderer_lifecycle_test` repeated `VK_AUDIT_SOAK`
   times (default 5)
6. libFuzzer: `utf8_fuzz`, `text_buffer_fuzz`, `html_strip_fuzz` in Vk_Canvas
   and `utf16_fuzz` in `App_shell/dev/fuzz`, each for
   `VK_AUDIT_FUZZ_SECONDS` (default 5)
7. `App_shell` build, header check and tests

Stage switches are documented at the top of the script. A skipped stage prints
why; it is never silently counted as a pass.

Evidence on 2026-10-31 (Intel UHD TGL, Mesa 26.2.4, GCC 16.2.1, Vulkan loader
1.4.363), re-run after the performance pass: 23/23 CTest entries pass (19 unit +
4 GPU), validation reports zero issues on all four
GPU binaries, the sanitizer run is leak-free, the soak passes 5/5, and the fuzz
targets (three in Vk_Canvas, one in App_shell) run ~10^6 inputs each with no
crash.

## Known limitations

- Android is not compiled in this checkout. `renderer_camera.cc` was updated to
  the same checked-call policy, but the Android build is not part of
  `tools/audit.sh`.
- The Windows/Android platform CMakeLists compile shaders through
  `cmake/VceShaders.cmake`; keep any other consumer's own Slang invocation in
  step with the SPIR-V 1.3 requirement.
- GPU CI against lavapipe is not wired; the probes run on local hardware.
- `VK_EXT_device_fault`-driven device-loss injection is not implemented.
