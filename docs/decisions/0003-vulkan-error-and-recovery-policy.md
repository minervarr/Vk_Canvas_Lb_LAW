# 0003 — Checked Vulkan calls, partial-init teardown and recovery states

Status: accepted (2026-10-31)

## Context

Initialisation mixed exceptions with unchecked `vkCreate*` calls. A failure
could leave a half-initialised renderer, and several layers treated "could not
create" as "quietly disable": the result was a renderer that ran but drew
nothing, with no diagnostic. Destructors and hot paths had no consistent policy
for a `VkResult` that is not part of the swapchain protocol.

Enabling the validation layer made three real defects visible, so the policy is
also what keeps those fixed:

- Slang's default SPIR-V 1.5 modules were invalid on the Vulkan 1.1 instance;
  the build now pins SPIR-V 1.3 (Slang → GLSL → glslang).
- `shaderDrawParameters` was used by the shaders but not requested; it is now
  enabled when the device reports it.
- The capture path reused an image index across a swapchain rebuild; the index
  is reset with the swapchain it belongs to.

## Decision

Every Vulkan call that returns `VkResult` and is not part of the swapchain
protocol goes through one of three macros in `core/gpu/vk_check.hh`:

| Macro | On failure | Used for |
|---|---|---|
| `VCE_VK_CHECK` | log with file/line, `abort()` | invariants and teardown, where continuing would touch a half-built object |
| `VCE_VK_CHECK_THROW` | log, throw `std::runtime_error` | constructors and init paths the caller can unwind |
| `VCE_VK_CHECK_OR(expr, action)` | log, run `action` (usually `return false`) | runtime upload/allocation paths that can degrade to a smaller frame |

Calls whose result is part of the protocol — `vkAcquireNextImageKHR`,
`vkQueuePresentKHR`, and the surface/device-loss family — are classified with
`vce_vk_classify()` and handled explicitly. `vkGetFenceStatus` is a query;
`VK_NOT_READY` is normal.

Initialisation is all-or-nothing: the constructor catches, calls the same
idempotent `cleanup()` the destructor uses, and rethrows. Teardown is the reverse
of creation and every destroy is null-checked. A memory-type lookup returns
`UINT32_MAX` when nothing matches; index 0 is a real type and is not used as
"not found".

Recovery states are values, not exceptions from a destructor or a per-frame
call:

- `VK_SUBOPTIMAL_KHR` → recreate the swapchain at the next safe point.
- `VK_ERROR_OUT_OF_DATE_KHR` → recreate and retry the acquire once.
- `VK_ERROR_SURFACE_LOST_KHR` → `surfaceLost()`; `draw()` stops until
  `recreate_surface()`.
- `VK_ERROR_DEVICE_LOST` → `deviceLost()`; `draw()` stops; the caller destroys
  and rebuilds the renderer.
- Allocation/upload failure → log and skip or shrink the draw.

`recreate_surface()` keeps the instance, device, pipelines, atlas and textures;
it rebuilds only surface, swapchain, framebuffers and command buffers. It
refuses to run after device loss, and it reports an incompatible
format/colour-space change instead of guessing.

No per-frame path calls `vkQueueWaitIdle` unless the caller explicitly asked for
a synchronous operation. Texture upload uses a fence and a deferred reaper; see
ADR 0008.

## Consequences

- A failed construction cannot leave a `Renderer` that exists but is invalid.
- A failure in a teardown invariant aborts instead of corrupting state; a
  failure in a runtime allocation degrades the frame.
- The validation stage in `tools/audit.sh` is a gate, not a report: any `VUID-`
  or `Validation Error` in the four GPU binaries fails the audit.
- `vk_result_test` pins the classifier; `renderer_lifecycle_test` exercises
  surface loss, resize, minimize and repeated create/destroy. Device loss cannot
  be injected without `VK_EXT_device_fault`; that limitation is recorded in
  `docs/RELIABILITY.md`.

## Rejected alternatives

- **Exceptions everywhere.** Vulkan errors occur in per-frame code and
  destructors; throwing there changes control flow and can terminate.
- **`abort()` everywhere.** A failed upload should shrink a frame, not kill the
  app.
- **Silent degradation.** This was the original behaviour and it produced black
  screens with healthy frame rates and empty logs.
