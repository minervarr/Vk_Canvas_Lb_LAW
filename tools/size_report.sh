#!/usr/bin/env bash
# Per-feature binary-size report for the "pay for what you use" property.
#
# Builds the same five feature-isolated consumers twice — once with function/
# data sections + --gc-sections (the shipped Release policy) and once without —
# and reports the linked, stripped executable size plus the module archives.
# Budgets live in tools/size_budgets.txt; exceeding one fails the script unless
# VK_SIZE_BUDGET=0.
#
#   tools/size_report.sh
#   VK_SIZE_BUDGET=0 tools/size_report.sh     # report only
#   VK_SIZE_DIR=... VK_SIZE_NOGC_DIR=... tools/size_report.sh
#
# Numbers are compiler/linker-specific: record the machine and toolchain with
# every baseline in docs/PERFORMANCE.md.
set -euo pipefail

VK_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SIZE_DIR="${VK_SIZE_DIR:-${TMPDIR:-/tmp}/opencode/vk-size}"
NOGC_DIR="${VK_SIZE_NOGC_DIR:-${TMPDIR:-/tmp}/opencode/vk-size-nogc}"
BUDGET_FILE="$VK_DIR/tools/size_budgets.txt"
VK_SIZE_BUDGET="${VK_SIZE_BUDGET:-1}"

PROBES=(size_layout size_canvas_rect size_ui_geometry size_ui_toggle size_ui_all_widgets size_gpu_renderer)
MODULES=(vk_canvas_base vk_canvas_draw vk_canvas_gpu vk_canvas_ui)

configure() {
    local dir="$1"; shift
    cmake -S "$VK_DIR/dev" -B "$dir" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DVCE_DEV_BUILD_SIZE_PROBES=ON \
        -DVCE_DEV_BUILD_SHADERS=OFF \
        -DVCE_DEV_BUILD_PLATFORM=OFF \
        -DVCE_DEV_BUILD_CAPTURE=OFF \
        "$@"
}

build_probes() {
    cmake --build "$1" --target "${PROBES[@]}"
}

# Echo the stripped size of an executable; the copy stays next to the build for
# inspection and is never part of the source tree.
stripped_size() {
    local dir="$1" name="$2"
    local src="$dir/size/$name"
    local dst="$dir/size/$name.stripped"
    cp "$src" "$dst"
    strip --strip-all "$dst"
    stat -c %s "$dst"
}

text_size() {
    local dir="$1" name="$2"
    size "$dir/size/$name" | awk 'NR == 2 { print $1 }'
}

echo "== size report: configure (section GC on)"
configure "$SIZE_DIR"
echo "== size report: build (section GC on)"
build_probes "$SIZE_DIR"

echo "== size report: configure (section GC off)"
configure "$NOGC_DIR" -DVCE_CANVAS_FUNCTION_SECTIONS=OFF
echo "== size report: build (section GC off)"
build_probes "$NOGC_DIR"

printf '\n## Linked, stripped feature probes (bytes)\n\n'
printf '| Probe | GC off | GC on | GC saves | GC on text |\n'
printf '|---|---:|---:|---:|---:|\n'
declare -A WITH_GC
for probe in "${PROBES[@]}"; do
    no_gc="$(stripped_size "$NOGC_DIR" "$probe")"
    gc="$(stripped_size "$SIZE_DIR" "$probe")"
    text="$(text_size "$SIZE_DIR" "$probe")"
    WITH_GC[$probe]="$gc"
    saved=$(( no_gc - gc ))
    printf '| `%s` | %s | %s | %s | %s |\n' \
        "$probe" "$no_gc" "$gc" "$saved" "$text"
done

printf '\n## Module archives (bytes)\n\n'
printf '| Archive | GC off | GC on | GC on text |\n'
printf '|---|---:|---:|---:|\n'
for module in "${MODULES[@]}"; do
    archive="lib${module}.a"
    no_gc_archive="$NOGC_DIR/core/$archive"
    gc_archive="$SIZE_DIR/core/$archive"
    if [ ! -f "$gc_archive" ]; then
        # ui/ is a sibling build directory, not a core subdirectory.
        no_gc_archive="$NOGC_DIR/ui/$archive"
        gc_archive="$SIZE_DIR/ui/$archive"
    fi
    no_gc="$(stat -c %s "$no_gc_archive")"
    gc="$(stat -c %s "$gc_archive")"
    text="$(size "$gc_archive" | awk 'NR == 2 { print $1 }')"
    printf '| `%s` | %s | %s | %s |\n' "$archive" "$no_gc" "$gc" "$text"
done

if [ "$VK_SIZE_BUDGET" = 0 ] || [ ! -f "$BUDGET_FILE" ]; then
    echo
    echo "size budgets: not checked"
    exit 0
fi

echo
echo "== size report: budgets ($BUDGET_FILE)"
failed=0
while read -r probe budget; do
    case "$probe" in ''|'#'*) continue ;; esac
    actual="${WITH_GC[$probe]:-}"
    if [ -z "$actual" ]; then
        echo "   $probe: no measurement"
        failed=1
        continue
    fi
    if [ "$actual" -le "$budget" ]; then
        echo "   $probe: $actual <= $budget ok"
    else
        echo "   $probe: $actual > $budget BUDGET EXCEEDED"
        failed=1
    fi
done < "$BUDGET_FILE"
if [ "$failed" != 0 ]; then
    echo "size report: budget failure"
    exit 1
fi
echo "size report: ok"
