# 0006 — Device, present mode, swapchain transform and output-target selection

Status: accepted (2026-10-31)

## Context

The renderer originally took `devs[0]` and queue family 0, requested
`samplerAnisotropy` unconditionally, always used FIFO, and set
`preTransform = caps.currentTransform`. Each of those is a reasonable default on
one machine and wrong on another:

- A compute-only family 0, or lavapipe enumerated first, cannot present to the
  window; the app either failed or rendered the UI on the CPU.
- Requesting an unsupported feature is invalid usage.
- FIFO plus a tight image count starved rendering when the Android compositor
  held an image for ~60 ms; the preview froze at about 1 Hz.
- `preTransform` is a promise, not a request. On Android, `currentTransform`
  becomes a rotation the moment the device turns; an app that draws upright then
  presents an upside-down, squashed frame (measured on a moto g06).

## Decision

**Device selection.** Enumerate every physical device; a candidate must expose
`VK_KHR_swapchain` and a graphics queue family that reports present support for
this surface. Prefer a non-CPU candidate and otherwise keep enumeration order.
Do not rank by device type: on a hybrid laptop that would silently move the app
to the dGPU and its battery, which is the user's decision. If no candidate
exists, fall back to device 0 / family 0 and log why; the later swapchain failure
is then diagnosable instead of looking like a broken UI.

**Features.** Enable only what the device reports: `samplerAnisotropy`,
`shaderStorageImageReadWithoutFormat`, `shaderStorageImageWriteWithoutFormat`
and `shaderDrawParameters`. The shaders use the vertex index, which declares the
DrawParameters capability; it must be enabled when supported and reported when
not. Mutable swapchain images require both `VK_KHR_swapchain_mutable_format` and
`VK_KHR_image_format_list`; enable both or neither.

**Present mode.** `PresentPolicy::Latency` (default) prefers `MAILBOX` when
advertised, otherwise `FIFO`. `PresentPolicy::Vsync` always uses `FIFO`. MAILBOX
never blocks the producer, so a compositor hold cannot stall rendering; FIFO is
the pacer when the consumer's frames are scheduled by a clock, and avoids a
render loop free-running over identical content.

**Image count.** Clamp the caller's request to the surface's `minImageCount` and
`maxImageCount` (0 means no limit), with a floor of 2. MAILBOX needs at least 3;
the constructor default leaves room for one image on screen, one queued and one
held during a compositor hitch. Each extra image is a full window-sized
allocation, so desktop callers request 3.

**Transform.** Ask for `VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR` when the surface
supports it, which hands rotation to the compositor. Fall back to
`caps.currentTransform` otherwise; refusing to create a swapchain would be worse
than a rotated one. The engine does not thread a rotation matrix through draw
calls.

**Output target.** `resolve_output_target()` runs exactly once, in the
constructor, before the first swapchain. `pickTarget()` resolves the consumer's
request against the surface's enumerated format/colour-space pairs and degrades
to the 8-bit SDR pin when the pair is unavailable. The render pass and every
pipeline bake the result in (`OUTPUT_ENCODE` specialization constant), and
`recreate_swapchain()` rebuilds neither, so re-resolving on resize would leave
the pipelines encoding for a target the swapchain no longer uses — silently
wrong colour, not a validation error. A runtime target switch requires a full
renderer rebuild. The surface's full format list is logged once per process
because real drivers differ and that log is the evidence for future choices.

`output_target.hh` is deliberately Vulkan-free; the renderer converts at the
swapchain boundary. HDR is always requested by the consumer, never guessed from
content, and each request degrades to SDR with a log line. The SDR pin matters
for ordinary streams: a camera preview is SDR, and presenting its HLG-encoded
pixels into the only 10-bit colourspace one phone exposed (BT2020 linear) made
the preview wash out, because the display read non-linear data as linear.

## Consequences

- The selected device is the first present-capable, non-CPU device in enumeration
  order; the log names it and marks software devices.
- One output target per process. Changing HDR mode at runtime means destroying
  and rebuilding the `Renderer`.
- A new surface that cannot provide the resolved format/colour-space pair makes
  `recreate_surface()` return `false`; the caller rebuilds instead of rendering
  into a mismatch.

## Evidence

`vk_result_test` covers the classifier, `output_target_test` covers the pure
picker and tone curves, and `renderer_lifecycle_test` covers resize, minimize and
surface loss. The present-mode and transform rationale comes from Android device
sessions; it is recorded here because the code must stay short.

## Rejected alternatives

- **Rank devices by type or score.** It overrides the user's hybrid-graphics
  choice and changes behaviour per machine.
- **MAILBOX always.** It burns power and renders identical frames when the
  consumer asked for vsync pacing.
- **`preTransform = currentTransform` always.** It is correct only for an app
  that pre-rotates its geometry.
- **Re-resolve the output target on resize.** The pipelines would keep the old
  encoding.
