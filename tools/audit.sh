#!/usr/bin/env bash
# One command for the Phase 0 foundation checks:
#   source inventory + layer rules, header self-containment, every unit test.
#
# Build trees are reused, never deleted. Override them with VK_BUILD_DIR and
# APP_SHELL_BUILD_DIR. The script is safe to run from any working directory.
set -euo pipefail

VK_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORKSPACE="$(cd "$VK_DIR/.." && pwd)"
APP_SHELL_DIR="$WORKSPACE/App_shell"

VK_BUILD_DIR="${VK_BUILD_DIR:-${TMPDIR:-/tmp}/opencode/vk-dev}"
APP_SHELL_BUILD_DIR="${APP_SHELL_BUILD_DIR:-${TMPDIR:-/tmp}/opencode/app-shell-dev}"
PYTHON="${PYTHON:-python3}"

echo "== dependency and source inventory"
"$PYTHON" "$VK_DIR/tools/check_deps.py"

echo "== vk_canvas: configure"
cmake -S "$VK_DIR/dev" -B "$VK_BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug

echo "== vk_canvas: header self-containment"
cmake --build "$VK_BUILD_DIR" --target check_headers

echo "== vk_canvas: build"
cmake --build "$VK_BUILD_DIR"

echo "== vk_canvas: tests"
ctest --test-dir "$VK_BUILD_DIR" --output-on-failure

if [ -d "$APP_SHELL_DIR/dev" ]; then
    echo "== app_shell: configure"
    cmake -S "$APP_SHELL_DIR/dev" -B "$APP_SHELL_BUILD_DIR" -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug

    echo "== app_shell: header self-containment"
    cmake --build "$APP_SHELL_BUILD_DIR" --target check_headers

    echo "== app_shell: build"
    cmake --build "$APP_SHELL_BUILD_DIR"

    echo "== app_shell: tests"
    ctest --test-dir "$APP_SHELL_BUILD_DIR" --output-on-failure
else
    echo "== app_shell: not present, skipping"
fi

echo "audit: ok"
