# 0007 — Android `AHardwareBuffer` external-image path

Status: accepted (2026-10-31)

## Context

On Android the decoder/camera hands the renderer an `AHardwareBuffer` that must
be imported as a Vulkan image and sampled through a
`VkSamplerYcbcrConversion`. This path accumulated device-specific fixes: the
buffer working set is larger than a camera preview's, the visible picture is
smaller than the allocation, the picture may be rotated by a quadrant, and the
pixel aspect ratio is not 1. Each mistake produced a plausible-looking but wrong
frame: black at a healthy frame rate, a landscape video letterboxed into a
portrait box, or a "magnifier" that showed the scene smaller than the preview.

The import chain requires, together: `VK_KHR_sampler_ycbcr_conversion`,
`VK_KHR_external_memory`, `VK_ANDROID_external_memory_android_hardware_buffer`,
`VK_EXT_queue_family_foreign`, `VK_KHR_bind_memory2` and
`VK_KHR_maintenance1`.

## Decision

**Handoff.** `update_camera_frame()` / `update_external_frame()` may be called
from a producer thread. They acquire the buffer, replace the pending handle under
`hwb_mutex_`, release the previous pending buffer and invoke its release
callback, then store `frame_ready_` and wake the render loop through
`FrameWaker`. The render thread consumes the pending buffer. Nothing else on this
path is thread-safe; see `docs/RELIABILITY.md`.

**Cache and eviction.** Imported images, memory, views and descriptor sets are
cached by `AHardwareBuffer*`, with an LRU list bounded by `kMaxCachedHwb = 32`.
A camera recycles a handful of gralloc buffers, but a decoder's `AImageReader`
was measured handing out 10 distinct buffers within one second; the old
10-descriptor pool was exhausted, `bind_hwb()` left the buffer uncached, the
draw lookup missed, and the screen went black at a perfectly healthy frame rate.
The pool is sized for that working set and uses
`VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT`, which is what makes
per-buffer eviction possible without resetting the whole pool. A session restart
(USB hotplug, lifecycle bounce) hands out new pointers; `clear_camera_frames()`
destroys the cached objects and resets the pool so a restart cannot exhaust it.

**Visible extent.** Letterbox and aspect calculations use
`external_visible_w_/h_` when set, not the allocation size. A 2048×1536 buffer
holding a 2040×1530 picture otherwise shows the padding and gets the aspect
ratio slightly wrong.

**Rotation and pixel aspect.** A quarter turn swaps the picture's width and
height, so the letterbox is computed on the rotated extent. The pixel aspect is
applied to the picture's own width first, then the rotation swaps the axes. The
other order stretches a portrait video along the wrong axis. The old code
unconditionally swapped the axes, which was right for the camera preview and
wrong for a landscape video that needs no rotation.

**Loupe.** The composite shader zooms in frame UV space, but the loupe is drawn
smaller than the main preview. A UV zoom of N lands on screen as
`N * (inset_width / preview_width)`, so the caller's preview-relative zoom is
converted before it is pushed. The inset rect is published in `loupe_rect_` so
the overlay can outline a window that would otherwise be invisible in a busy
frame.

**Failures are loud, once.** Every failure in `bind_hwb()` used to return in
silence, which presents as a black screen with no log line. `vkCreateImage`,
import, memory allocation, view creation and descriptor updates now log the
external format, size and reason.

**Colour description.** `set_external_colour()` configures the YCbCr conversion
from the producer's format/colour-space/range; it is ignored loudly after the
conversion object exists. Rotation, visible size, transfer function and pixel
aspect are configuration calls made before the first frame that consumes the
image (see `docs/RELIABILITY.md`).

## Consequences

- The descriptor pool is bounded but sized for a video stream; exceeding 32 live
  buffers evicts the least-recently-used one and re-imports it if it returns.
- Imported buffers are not freed until the render thread has consumed them and
  their release callbacks have run.
- Android is not compiled in this checkout. The path is source-reviewed and kept
  under the same checked-call policy as the rest of the renderer, but
  `tools/audit.sh` does not build it; a device build remains the verification.

## Rejected alternatives

- **Unbounded descriptor pool.** It hides the working-set growth until driver
  allocation fails; the bound plus eviction was the measured fix.
- **`vkQueueWaitIdle` per imported buffer.** It stalls the render loop at video
  rate and is not needed: same-queue submission order and the release callback
  already keep lifetime safe.
- **Pre-rotating geometry in the engine.** It would thread a rotation matrix
  through every draw call; asking the compositor for IDENTITY is cheaper (ADR
  0006).
