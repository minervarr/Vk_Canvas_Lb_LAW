#!/usr/bin/env python3
"""Enforce the layer rules and the source inventory for vk_canvas.

This is intentionally a source-level check, not a compiler. It catches the two
mistakes that erode auditability fastest:

  * a core file including a platform SDK or an unported prototype;
  * a .cc file in core/ that no CMake target compiles.

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
SOURCES_RE = re.compile(r"add_library\s*\(\s*vk_canvas_core\s+STATIC(.*?)\)", re.S)

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
    "overlay.hh",
    "image_layer.hh",
    "platform.hh",
    "headless.hh",
    "art_texture.hh",
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


def check_engine_includes(errors: list[str]) -> None:
    engine_files = repo_files(CORE, (".hh", ".cc"))
    for path in engine_files:
        rel = path.relative_to(ROOT).as_posix()
        # The capture tool is a dev tool and may use the Wayland backend.
        if path.relative_to(CORE).parts[0] == "capture":
            continue

        allowed = ALLOWED_CORE_PLATFORM.get(path.name, set())
        for inc in includes_of(path):
            if is_platform_include(inc) and inc not in allowed:
                errors.append(
                    f"{rel}: platform SDK include <{inc}>"
                )
            if inc in WIP_HEADERS or inc.startswith("wip/"):
                errors.append(
                    f"{rel}: includes unported prototype {inc!r}"
                )
            if inc.startswith("ui/"):
                errors.append(
                    f"{rel}: core must not include the ui layer ({inc!r})"
                )


def check_ui_includes(errors: list[str]) -> None:
    if not UI.is_dir():
        return
    for path in repo_files(UI, (".hh", ".cc")):
        rel = path.relative_to(ROOT).as_posix()
        for inc in includes_of(path):
            if inc in UI_FORBIDDEN or inc.startswith("platform/"):
                errors.append(
                    f"{rel}: ui must not include the GPU/platform layer ({inc!r})"
                )
            if is_platform_include(inc):
                errors.append(
                    f"{rel}: ui must not include a platform SDK header ({inc!r})"
                )


def check_core_inventory(errors: list[str]) -> int:
    cmake = CORE / "CMakeLists.txt"
    if not cmake.is_file():
        errors.append("core/CMakeLists.txt is missing")
        return 0
    text = cmake.read_text(encoding="utf-8", errors="replace")
    match = SOURCES_RE.search(text)
    if not match:
        errors.append("core/CMakeLists.txt: vk_canvas_core source list not found")
        return 0

    listed = {
        token.strip()
        for token in match.group(1).split()
        if token.strip().endswith(".cc")
    }
    actual = {p.name for p in CORE.glob("*.cc")}
    for name in sorted(actual - listed):
        errors.append(f"core/{name}: not in vk_canvas_core source list")
    for name in sorted(listed - actual):
        errors.append(f"core/{name}: listed in CMake but missing on disk")

    # Nothing under wip/ may be compiled by any target.
    for cm in repo_files(ROOT, (".txt", ".cmake")):
        if cm.name != "CMakeLists.txt" and cm.suffix != ".cmake":
            continue
        text = cm.read_text(encoding="utf-8", errors="replace")
        for token in re.findall(r"[^\s()]+", text):
            if token.startswith("wip/") and token.endswith((".cc", ".c")):
                errors.append(f"{cm.relative_to(ROOT)}: compiles {token}")
    return len(actual)


def main() -> int:
    errors: list[str] = []
    check_engine_includes(errors)
    check_ui_includes(errors)
    compiled = check_core_inventory(errors)

    if errors:
        print("check_deps: FAILED", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    core_files = len(repo_files(CORE, (".hh", ".cc")))
    print(f"check_deps: ok ({core_files} core files, {compiled} compiled sources)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
