# Decision records

This directory holds the rationale behind choices that are expensive to reverse.
Code and headers state the contract; an ADR states why the contract is that way,
what was rejected, and what evidence exists.

Rules:

- One decision per file, `NNNN-short-title.md`, numbered in order, never reused.
- Status is `accepted`, `superseded by NNNN`, or `proposed`.
- An ADR may be amended for accuracy. A changed decision gets a new ADR that
  supersedes the old one; history is not rewritten.
- Keep measured evidence and device names here when the decision depended on a
  bug or a measurement. That is the knowledge a code comment cannot hold without
  burying the code.
- Code comments point here for rationale and keep only invariants, contracts and
  the one fact a local reader needs.

Index:

| ADR | Decision | Status |
|---|---|---|
| [0001](0001-module-boundaries-and-compatibility.md) | Module directories, CMake targets and the compatibility umbrella | accepted |
| [0002](0002-renderer-pimpl-and-tu-split.md) | `Renderer` pimpl and subsystem translation units | accepted |
| [0003](0003-vulkan-error-and-recovery-policy.md) | Checked Vulkan calls, partial-init teardown and recovery states | accepted |
| [0004](0004-pay-for-use-and-size-budgets.md) | Pay-for-use: per-subsystem TUs, section GC, size budgets, no generator | accepted |
| [0005](0005-immediate-mode-ui-context.md) | Immediate-mode `ui::Context` with toolkit-owned state | accepted |
| [0006](0006-device-swapchain-and-output.md) | Device, present mode, swapchain transform and output-target selection | accepted |
| [0007](0007-android-external-image-path.md) | Android `AHardwareBuffer` external-image path | accepted |
| [0008](0008-growing-gpu-buffer-capacity.md) | Growing GPU buffers instead of silent truncation | accepted |
| [0009](0009-text-fit-and-type-scale.md) | One type scale, one-pass fit and the solid-texel fallback | accepted |
| [0010](0010-repository-topology.md) | `app_shell/` lives inside Vk_Canvas; source and target boundaries unchanged | accepted |

Related canonical documents: [`../ARCHITECTURE.md`](../ARCHITECTURE.md),
[`../RELIABILITY.md`](../RELIABILITY.md), [`../PERFORMANCE.md`](../PERFORMANCE.md),
[`../ROADMAP.md`](../ROADMAP.md).
