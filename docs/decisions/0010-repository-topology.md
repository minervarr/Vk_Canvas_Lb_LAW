# 0010 — Repository topology: app_shell lives inside Vk_Canvas

Status: accepted (2026-10-31)

## Context

`App_shell` was a separate repository, added as a submodule at
`framework/app_shell` by every consumer. The split was meant to protect one
property: nothing in the shell may learn application concepts, and the engine
must not depend on the shell.

In practice the repository boundary was the weakest of the three boundaries.
The source coupling already crossed it:

- The Win32, Wayland and Android hosts include concrete vk_canvas headers
  (`win32_platform.hh`, `wayland_display.hh`, `wayland_window.hh`,
  `renderer.hh`, `jni_util.hh`, `fullscreen.hh`).
- The portable half includes `type_scale.hh` and `frame_input.hh`.
- Every consumer found (Matrix_Player, VideoPlayer, whisper_ui_cpp) pinned both
  repositories. The one repository that uses `audio_engine` alone
  (media_player) does not use App_shell.
- `tools/audit.sh` reached outside the repository to `$WORKSPACE/App_shell` and
  silently skipped the shell when the sibling checkout was absent.
- Consumers pinned the pair by hand. Nothing rejected a new App_shell against
  an old canvas, and a change that spanned both needed two commits in a fixed
  order.
- App_shell's default `VK_CANVAS_DIR` (`../vk_canvas`) did not exist in a
  normal checkout; every consumer passed the path explicitly.

`docs/ARCHITECTURE.md`, `docs/ROADMAP.md` and `docs/VISION.md` already described
app_shell as a layer of the same architecture. Only the version-control
boundary said otherwise.

## Decision

`app_shell/` is a top-level directory in `Vk_Canvas_Lb_LAW`, a sibling of
`core/`, `ui/` and `platform/`.

- The source and CMake-target boundaries are unchanged: `app_shell` (portable,
  links no engine target), `app_shell_win32`, `app_shell_wayland` and
  `app_shell_android`; the `APP_SHELL_*` cache variables keep their names and
  defaults.
- The repository still has **no root CMakeLists.txt**. A consumer adds
  `core/`, `app_shell/` and exactly one platform backend.
- The dependency direction stays `app_shell -> core/platform`, never the
  reverse. Nothing under `core/`, `ui/` or `platform/` may include an app_shell
  header.
- The import preserves history via a filtered `git subtree`. The temporary
  clone dropped the tracked `build/` tree and `compile_commands.json`; the
  source repository was not rewritten.
- `app_shell/` is covered by the root AGPLv3 license. It has no separate
  license file.

`app_shell/dev/` is retained as a standalone harness for working on the shell
alone. The framework's `dev/` tree is the canonical build: one configure, one
`ctest`, one audit.

## Consequences

- One checkout, one `ctest` and one `tools/audit.sh` cover both libraries. The
  audit no longer looks outside the repository.
- A consumer drops the `framework/app_shell` submodule and points at
  `framework/vk_canvas/app_shell`; its vk_canvas pin carries the shell too.
- A change that spans the engine and the shell is one commit. The old
  "commit Vk_Canvas before App_shell" rule is retired.
- The shell is now AGPLv3 with the engine. It was already unusable without the
  engine, because the hosts include and link it.
- `tools/check_deps.py` enforces the portable/host split and the reverse edge.
  This replaces the enforcement the repository split was providing.
- The standalone `minervarr/App_shell` repository is archived, not deleted.
  Existing branches can still pin it.

## Alternatives considered

- **Keep two repositories.** Rejected: it preserves a cross-repository
  transaction, an unpinned version pair, and an audit that is not
  self-contained, while the source coupling already crosses the boundary.
- **Keep App_shell as a nested submodule of Vk_Canvas.** Rejected: it preserves
  the two-commit transaction and adds submodule recursion without making the
  pair atomic.
- **Merge app_shell into `core/` or `ui/`.** Rejected: it would put window and
  application lifecycle code below the layers that must not know about it, and
  break the dependency table.
- **Extract the platform backends from Vk_Canvas instead.** Rejected as a
  separate, much larger refactor: the platform layer is part of the engine's
  value (three real backends), and the renderer owns the surface/swapchain
  boundary.

## Enforcement

- `tools/check_deps.py`:
  - portable `app_shell/*` may include base/input/layout only;
  - `app_shell/os/*` may include platform SDKs and vk platform/gpu headers;
  - no `core/`, `ui/` or `platform/` file may include an app_shell header;
  - every compiled shell source must be named in `app_shell/CMakeLists.txt`.
- `dev/CMakeLists.txt` includes the top-level app_shell headers in
  `check_headers`.
- `docs/ARCHITECTURE.md` and `docs/MODULES.md` state the directory boundary.

## Evidence

- Import commit: `311b8a6` (`app_shell/` from the filtered App_shell history).
- `Vk_Canvas_Lb_LAW/tools/audit.sh` green with 27/27 CTest entries (23 unit +
  4 GPU), including the four app_shell tests.
- `check_deps: ok` with the shell rules enabled.
