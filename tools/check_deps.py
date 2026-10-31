#!/usr/bin/env python3
"""Enforce the layer rules and the source inventory for vk_canvas.

This is intentionally a source-level check, not a compiler. It catches the
mistakes that erode auditability fastest:

  * a module including a layer above it (base -> draw, draw -> gpu, any -> ui);
  * a core file including a platform SDK or an unported prototype;
  * a .cc file under core/ that no module target compiles, or a stale entry.

Run from anywhere: the repository root is derived from this file's location.
Exit status is non-zero on the first failing category, with every finding
printed.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / "core"
WIP = ROOT / "wip"
UI = ROOT / "ui"

INCLUDE_RE = re.compile(r'^\s*#\s*include\s+[<"]([^">]+)[">]', re.MULTILINE)
MODULE_RE = re.compile(
    r"add_library\s*\(\s*(vk_canvas_(?:base|draw|gpu))\s+STATIC(.*?)\)", re.S
)
UI_MODULE_RE = re.compile(
    r"add_library\s*\(\s*vk_canvas_ui\s+STATIC(.*?)\)", re.S
)
UI_DEMO_RE = re.compile(
    r"add_library\s*\(\s*vk_canvas_ui_demo\s+STATIC(.*?)\)", re.S
)

# Physical module -> the CMake target that owns it. base also owns input/ and
# layout/: they are separate directories to keep the dependency arrows honest,
# but one archive because every consumer needs the value types anyway.
MODULES = {
    "base": ("base", "input", "layout"),
    "draw": ("draw",),
    "gpu": ("gpu",),
}
TARGET_BY_MODULE = {
    "base": "vk_canvas_base",
    "draw": "vk_canvas_draw",
    "gpu": "vk_canvas_gpu",
}

# Layer edges allowed for each source directory. A module may include its own
# directory and the ones below it, never the ones above it.
ALLOWED_LAYERS = {
    "base": {"base"},
    "input": {"base", "input"},
    "layout": {"base", "layout"},
    "draw": {"base", "input", "layout", "draw"},
    "gpu": {"base", "input", "layout", "draw", "gpu"},
}

# Platform SDK headers that must never appear in the portable engine. Vulkan
# itself is allowed (the seam and the renderer need it); the platform-specific
# Vulkan headers are not.
PLATFORM_PREFIXES = (
    "android/",
    "jni.h",
    "windows.h",
    "winsock",
    "shellapi.h",
    "wayland-client.h",
    "wayland-cursor.h",
    "wayland-server",
    "xkbcommon/",
    "vulkan/vulkan_android.h",
    "vulkan/vulkan_win32.h",
    "vulkan/vulkan_wayland.h",
    "vulkan/vulkan_xlib.h",
    "vulkan/vulkan_xcb.h",
    "vulkan/vulkan_metal.h",
    "vulkan/vulkan_macos.h",
)

# Two deliberate, guarded exceptions. Both are seam code that the renderer's
# Android camera path needs; Phase 1 moves the camera half into a private
# source file. Add an entry here only with a comment explaining why.
ALLOWED_CORE_PLATFORM = {
    "log.hh": {"android/log.h"},
    "renderer.hh": {"android/hardware_buffer.h", "vulkan/vulkan_android.h"},
}

WIP_HEADERS = {
    "archive.hh",
    "canvas_host.hh",
    "compute_context.hh",
    "font_weight.hh",
    "text_editor.hh",
    "vulkan_state.hh",
}

UI_FORBIDDEN = {
    "renderer.hh",
    "renderer_impl.hh",
    "overlay.hh",
    "image_layer.hh",
    "platform.hh",
    "headless.hh",
    "art_texture.hh",
    "output_target.hh",
    "vk_check.hh",
}


def repo_files(directory: Path, suffixes: tuple[str, ...]) -> list[Path]:
    if not directory.is_dir():
        return []
    return sorted(
        p for p in directory.rglob("*")
        if p.is_file() and p.suffix in suffixes
    )


def includes_of(path: Path) -> list[str]:
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return []
    return INCLUDE_RE.findall(text)


def is_platform_include(name: str) -> bool:
    return any(name == p or name.startswith(p) for p in PLATFORM_PREFIXES)


def module_of_path(path: Path) -> str | None:
    """Return the module directory a core source/header lives in."""
    try:
        parts = path.relative_to(CORE).parts
    except ValueError:
        return None
    if not parts:
        return None
    return parts[0] if parts[0] in ALLOWED_LAYERS else None


def core_header_layers() -> dict[str, str]:
    """Map a public header name to the layer that owns it."""
    owners: dict[str, str] = {}
    for directory in ALLOWED_LAYERS:
        for path in repo_files(CORE / directory, (".hh",)):
            owners.setdefault(path.name, directory)
    for path in repo_files(UI, (".hh",)):
        owners.setdefault(path.name, "ui")
    return owners


def check_module_includes(errors: list[str]) -> None:
    owners = core_header_layers()
    engine_files = repo_files(CORE, (".hh", ".cc"))
    for path in engine_files:
        rel = path.relative_to(ROOT).as_posix()
        # capture/ and tests/ are consumers of the engine, not layers.
        top = path.relative_to(CORE).parts[0]
        if top in ("capture", "tests"):
            continue

        layer = module_of_path(path)
        if layer is None:
            errors.append(f"{rel}: source is not inside a module directory")
            continue

        allowed = ALLOWED_LAYERS[layer]
        allowed_platform = ALLOWED_CORE_PLATFORM.get(path.name, set())
        for inc in includes_of(path):
            if is_platform_include(inc) and inc not in allowed_platform:
                errors.append(f"{rel}: platform SDK include <{inc}>")
            if inc in WIP_HEADERS or inc.startswith("wip/"):
                errors.append(f"{rel}: includes unported prototype {inc!r}")
            if inc.startswith("ui/"):
                errors.append(f"{rel}: core must not include the ui layer ({inc!r})")

            owner = owners.get(Path(inc).name)
            if owner is not None and owner not in allowed:
                errors.append(
                    f"{rel}: {layer} must not include the {owner} layer ({inc!r})"
                )


def check_ui_includes(errors: list[str]) -> None:
    if not UI.is_dir():
        return
    owners = core_header_layers()
    for path in repo_files(UI, (".hh", ".cc")):
        rel = path.relative_to(ROOT).as_posix()
        for inc in includes_of(path):
            if inc in UI_FORBIDDEN or inc.startswith("platform/"):
                errors.append(
                    f"{rel}: ui must not include the GPU/platform layer ({inc!r})"
                )
            if inc.startswith("vulkan/") or inc == "vulkan.h":
                errors.append(
                    f"{rel}: ui must not include Vulkan ({inc!r})"
                )
            if is_platform_include(inc):
                errors.append(
                    f"{rel}: ui must not include a platform SDK header ({inc!r})"
                )
            owner = owners.get(Path(inc).name)
            if owner == "gpu":
                errors.append(
                    f"{rel}: ui must not include the gpu layer ({inc!r})"
                )


def check_core_inventory(errors: list[str]) -> int:
    cmake = CORE / "CMakeLists.txt"
    if not cmake.is_file():
        errors.append("core/CMakeLists.txt is missing")
        return 0
    text = cmake.read_text(encoding="utf-8", errors="replace")

    listed: dict[str, set[str]] = {}
    for target, body in MODULE_RE.findall(text):
        module = next(m for m, t in TARGET_BY_MODULE.items() if t == target)
        sources = {
            token.strip()
            for token in body.split()
            if token.strip().endswith(".cc")
        }
        listed[module] = sources

    for module, target in TARGET_BY_MODULE.items():
        if module not in listed:
            errors.append(f"core/CMakeLists.txt: {target} source list not found")
            continue
        actual = {
            p.relative_to(CORE).as_posix()
            for directory in MODULES[module]
            for p in (CORE / directory).glob("*.cc")
        }
        for name in sorted(actual - listed[module]):
            errors.append(f"core/{name}: not in {target} source list")
        for name in sorted(listed[module] - actual):
            errors.append(f"core/{name}: listed in {target} but missing on disk")

    # A file must belong to exactly one target.
    seen: dict[str, str] = {}
    for module, sources in listed.items():
        for name in sources:
            if name in seen:
                errors.append(
                    f"core/{name}: compiled by both {seen[name]} and "
                    f"{TARGET_BY_MODULE[module]}"
                )
            seen[name] = TARGET_BY_MODULE[module]

    # No stray .cc at core/ root: everything belongs to a module.
    for path in CORE.glob("*.cc"):
        errors.append(
            f"core/{path.name}: not inside a module directory (move it to "
            f"base/, draw/ or gpu/)"
        )

    # Nothing under wip/ may be compiled by any target.
    for cm in repo_files(ROOT, (".txt", ".cmake")):
        if cm.name != "CMakeLists.txt" and cm.suffix != ".cmake":
            continue
        text = cm.read_text(encoding="utf-8", errors="replace")
        for token in re.findall(r"[^\s()]+", text):
            if token.startswith("wip/") and token.endswith((".cc", ".c")):
                errors.append(f"{cm.relative_to(ROOT)}: compiles {token}")

    return sum(len(s) for s in listed.values())


def check_ui_inventory(errors: list[str]) -> int:
    cmake = UI / "CMakeLists.txt"
    if not UI.is_dir():
        return 0
    if not cmake.is_file():
        errors.append("ui/CMakeLists.txt is missing")
        return 0
    text = cmake.read_text(encoding="utf-8", errors="replace")
    match = UI_MODULE_RE.search(text)
    if not match:
        errors.append("ui/CMakeLists.txt: vk_canvas_ui source list not found")
        return 0

    listed = {
        token.strip()
        for token in match.group(1).split()
        if token.strip().endswith(".cc")
    }
    # demo/ is a separate consumer target, not part of the toolkit archive.
    actual = {
        p.relative_to(UI).as_posix()
        for p in repo_files(UI, (".cc",))
        if p.relative_to(UI).parts[0] not in ("tests", "demo")
    }
    for name in sorted(actual - listed):
        errors.append(f"ui/{name}: not in vk_canvas_ui source list")
    for name in sorted(listed - actual):
        errors.append(f"ui/{name}: listed in CMake but missing on disk")

    demo_match = UI_DEMO_RE.search(text)
    demo_actual = {
        p.relative_to(UI).as_posix()
        for p in repo_files(UI / "demo", (".cc",))
    }
    if demo_actual and not demo_match:
        errors.append("ui/CMakeLists.txt: vk_canvas_ui_demo source list not found")
    elif demo_match:
        demo_listed = {
            token.strip()
            for token in demo_match.group(1).split()
            if token.strip().endswith(".cc")
        }
        for name in sorted(demo_actual - demo_listed):
            errors.append(f"ui/{name}: not in vk_canvas_ui_demo source list")
        for name in sorted(demo_listed - demo_actual):
            errors.append(f"ui/{name}: listed in vk_canvas_ui_demo but missing on disk")
    return len(actual) + len(demo_actual)


def main() -> int:
    errors: list[str] = []
    check_module_includes(errors)
    check_ui_includes(errors)
    compiled = check_core_inventory(errors)
    compiled += check_ui_inventory(errors)

    if errors:
        print("check_deps: FAILED", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    core_files = len(repo_files(CORE, (".hh", ".cc")))
    ui_files = len(repo_files(UI, (".hh", ".cc")))
    print(
        f"check_deps: ok ({core_files} core + {ui_files} ui files, "
        f"{compiled} compiled sources across {len(MODULES) + 1} module targets)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
