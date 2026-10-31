# 0002 — `Renderer` pimpl and subsystem translation units

Status: accepted (2026-10-31)

## Context

`core/gpu/renderer.cc` was 2,849 lines and `renderer.hh` exposed more than 60
members: instance/device/queue setup, swapchain and render passes, Android camera
import, MSDF atlas and text pipeline, SDF shapes, readback and frame pacing. One
`Renderer` object file therefore pulled every subsystem into any consumer, and
the public header dragged in `overlay.hh`, `image_layer.hh` and font-atlas
internals. Changing a private field recompiled every consumer.

Splitting the file was necessary, but the public header also had to stop being
the private state definition.

## Decision

`Renderer` holds `std::unique_ptr<Impl>`. The public header declares the API and
the pimpl; `Impl` lives in `renderer_impl.hh`, which is private to `core/gpu`.
Public member functions are thin forwarders in `renderer_api.cc`; they are the
only place that touches `Impl` from the public surface.

The implementation is split by subsystem:

```
renderer.cc            Impl ctor/dtor, draw(), cleanup
renderer_api.cc        public forwarders
renderer_device.cc     instance, surface, physical/logical device, caps, output target
renderer_swapchain.cc  swapchain, render passes, framebuffers, sync, text composite
renderer_camera.cc     external images, AHardwareBuffer/YCbCr (Android)
renderer_text.cc       MSDF/MTSDF atlas, text VBO, atlas updates
renderer_text_pipeline.cc  text pipeline, record, cleanup
renderer_shapes.cc     SDF shape VBO/pipeline
renderer_readback.cc   readback
```

`Renderer` is non-copyable and non-movable. `OverlayRasterizer` and
`ImageLayer` are non-copyable owners with explicit `cleanup()`.

## Consequences

- The public header no longer exposes overlay, image-layer or atlas internals.
- Compile time and archive granularity improve: a consumer that pulls the
  renderer object pulls `renderer.o`, and the other subsystem objects only when
  their symbols are referenced. The largest renderer TU was 534 lines at the
  split; `overlay.cc` is larger (677 lines) but is one subsystem, not a mixed
  TU. `docs/MODULES.md` keeps the current line counts.
- Construction has exactly one cleanup path: `cleanup()` is idempotent and the
  constructor's catch block calls it, so a throw after `vkCreateInstance` does
  not leak earlier objects. See ADR 0003.
- An ABI change is unavoidable (the object layout changes), but the source API
  is unchanged. This is acceptable because no binary-compatibility promise
  exists between these libraries and their consumers.

## Rejected alternatives

- **Split the `.cc` without a pimpl.** The public header would still include
  every private type and recompile consumers on private changes.
- **Keep one file and rely on section GC.** GC removes unused functions at link
  time but does not fix the header surface, compile time or audit readability.
- **An abstract interface with virtual calls.** The renderer is a per-frame hot
  path and has one implementation; a vtable would add indirection for no
  extension point anyone asked for.
