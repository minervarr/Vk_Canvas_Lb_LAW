# 0008 — Growing GPU buffers instead of silent truncation

Status: accepted (2026-10-31)

## Context

The text and SDF-shape vertex buffers started at fixed capacities and clamped
overflow:

```cpp
if (verts > kMaxMsdfVerts) verts = kMaxMsdfVerts;
```

Two failure modes followed:

- The clamp was not quad-aligned. 16,384 is not a multiple of the 6 vertices in a
  glyph quad, so one malformed triangle was left with a halfway UV. A dense
  screen at high resolution reaches ~2,730 glyphs easily; "the text stops halfway
  down the page" was a real ceiling.
- Bitmap UI text (Terminus, one rect per pixel-run) hit the 2,048-quad shape
  ceiling after a few dozen letters, so later rows and everything drawn after
  them vanished mid-glyph.

The same pattern existed in texture creation: a synchronous `vkQueueWaitIdle`
per upload and per destroy made grid-art streaming hitch while scrolling.

## Decision

**Vertex buffers grow by doubling, quad-aligned.** `ensureMsdfVboCapacity()` and
`ensureShapeVboCapacity()` start at 16,384 vertices and 6×2,048 vertices
respectively, double until the frame fits, and round the capacity down to a whole
quad or glyph. They are called only after the frame slot's fence has been waited
on, so the old buffer is no longer in flight when it is unmapped and destroyed.
Per-frame slots keep their capacity; the buffer never shrinks, so the working-set
high-water mark is paid once rather than reallocated every frame. Allocation
failure is logged and returns false; the frame is skipped or reduced, never
half-written.

**The atlas re-uploads only dirty pages.** `updateMsdfAtlasPages()` packs the
pages the font reports as changed into one staging buffer, keeps the image, view,
sampler, descriptor set and pipeline, and copies each page to its real array
layer. A lazy glyph bake therefore does not rebuild the pipeline.

**Texture lifetime is deferred, not synchronised.** `create_texture()` submits
its upload with a fence rather than `vkQueueWaitIdle`; same-queue submission
order already guarantees the upload lands before any later frame samples it.
`destroy_texture()` unlinks the handle; `collectGarbage(queueDrained)` does the
actual Vulkan destruction once every command buffer that could bind it has
finished. `queueDrained=true` means the caller has just proven all previously
submitted work finished (`draw()` after its frame-fence wait, `cleanup()` after
device idle). With `queueDrained=false`, only fence-signalled upload staging is
reclaimed. This removed a full idle wait per create and per destroy.

## Consequences

- Capacity tracks the largest frame seen; a scene that later shrinks keeps the
  larger allocation for the process lifetime. This is the deliberate trade: no
  per-frame reallocation and no mid-glyph truncation.
- The initial capacity is not a limit. A caller does not need to size anything.
- A failed growth returns false; the draw path must treat it as "skip this
  frame", not as "draw a truncated frame".

## Rejected alternatives

- **Clamp at the ceiling.** It silently corrupts or drops content, which is
  worse than a skipped frame.
- **Exact-size reallocation.** A scene that creeps past capacity one glyph at a
  time would reallocate every frame.
- **`vkQueueWaitIdle` on upload/destroy.** It serialises the render loop and was
  the measured streaming hitch.
