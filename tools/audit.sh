#!/usr/bin/env bash
# One command for the foundation checks:
#   source inventory + layer rules, header self-containment, every unit test,
#   and (Phase 3) validation, sanitizer, soak and fuzz stages.
#
# Build trees are reused, never deleted. Override them with VK_BUILD_DIR and
# APP_SHELL_BUILD_DIR. The script is safe to run from any working directory.
#
# Stage switches (defaults run everything the machine supports):
#   VK_AUDIT_VALIDATION=0     skip the VK_LAYER_KHRONOS_validation build
#   VK_AUDIT_SANITIZE=0       skip the ASan+UBSan build
#   VK_AUDIT_SOAK=0           skip repeated lifecycle/smoke runs
#   VK_AUDIT_FUZZ_SECONDS=0   skip libFuzzer
#   VK_AUDIT_SIZE=0           skip the Release per-feature size report
set -euo pipefail

VK_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORKSPACE="$(cd "$VK_DIR/.." && pwd)"
APP_SHELL_DIR="$WORKSPACE/App_shell"

VK_BUILD_DIR="${VK_BUILD_DIR:-${TMPDIR:-/tmp}/opencode/vk-dev}"
APP_SHELL_BUILD_DIR="${APP_SHELL_BUILD_DIR:-${TMPDIR:-/tmp}/opencode/app-shell-dev}"
VK_VALIDATION_DIR="${VK_VALIDATION_DIR:-${VK_BUILD_DIR}-validation}"
VK_SANITIZE_DIR="${VK_SANITIZE_DIR:-${VK_BUILD_DIR}-sanitize}"
VK_FUZZ_DIR="${VK_FUZZ_DIR:-${VK_BUILD_DIR}-fuzz}"
PYTHON="${PYTHON:-python3}"

VK_AUDIT_VALIDATION="${VK_AUDIT_VALIDATION:-1}"
VK_AUDIT_SANITIZE="${VK_AUDIT_SANITIZE:-1}"
VK_AUDIT_SOAK="${VK_AUDIT_SOAK:-5}"
VK_AUDIT_FUZZ_SECONDS="${VK_AUDIT_FUZZ_SECONDS:-5}"
VK_AUDIT_SIZE="${VK_AUDIT_SIZE:-1}"

GPU_TESTS="text_edge_probe shape_outline_probe gpu_smoke_test renderer_lifecycle_test"

configure_vk() {
    local dir="$1"; shift
    cmake -S "$VK_DIR/dev" -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=Debug "$@"
}

echo "== dependency and source inventory"
"$PYTHON" "$VK_DIR/tools/check_deps.py"

echo "== vk_canvas: configure"
configure_vk "$VK_BUILD_DIR"

echo "== vk_canvas: header self-containment"
cmake --build "$VK_BUILD_DIR" --target check_headers

echo "== vk_canvas: build"
cmake --build "$VK_BUILD_DIR"

echo "== vk_canvas: tests"
ctest --test-dir "$VK_BUILD_DIR" --output-on-failure

if [ "$VK_AUDIT_SIZE" != 0 ]; then
    echo "== vk_canvas: per-feature binary size"
    "$VK_DIR/tools/size_report.sh"
fi

if [ "$VK_AUDIT_VALIDATION" != 0 ]; then
    echo "== vk_canvas: validation layer"
    validation_found=0
    for layer_dir in \
        /usr/share/vulkan/explicit_layer.d \
        /usr/local/share/vulkan/explicit_layer.d \
        "${HOME:-/nonexistent}/.local/share/vulkan/explicit_layer.d" \
        "${VULKAN_SDK:-/nonexistent}/share/vulkan/explicit_layer.d"; do
        if [ -e "$layer_dir/VkLayer_khronos_validation.json" ]; then
            validation_found=1
        fi
    done
    if [ "$validation_found" = 0 ]; then
        echo "   validation layer not installed; skipping"
    else
        configure_vk "$VK_VALIDATION_DIR" -DVCE_VK_VALIDATION=ON
        cmake --build "$VK_VALIDATION_DIR"
        for t in $GPU_TESTS; do
            set +e
            out="$("$VK_VALIDATION_DIR/core/$t" 2>&1)"
            rc=$?
            set -e
            printf '%s\n' "$out" > "$VK_VALIDATION_DIR/$t.validation.log"
            if [ "$rc" -eq 77 ]; then
                echo "   $t: skipped (77)"
                continue
            fi
            if [ "$rc" -ne 0 ]; then
                echo "   $t: FAILED (exit $rc)"
                printf '%s\n' "$out" | tail -40
                exit 1
            fi
            if printf '%s\n' "$out" | grep -Eq 'VUID-|Validation Error'; then
                echo "   $t: validation errors (see $VK_VALIDATION_DIR/$t.validation.log)"
                printf '%s\n' "$out" | grep -E 'VUID-|Validation Error' | head -20
                exit 1
            fi
            echo "   $t: clean"
        done
    fi
fi

if [ "$VK_AUDIT_SANITIZE" != 0 ]; then
    echo "== vk_canvas: address+UB sanitizer"
    configure_vk "$VK_SANITIZE_DIR" -DVCE_DEV_SANITIZE=ON
    cmake --build "$VK_SANITIZE_DIR"
    # Leak detection is on for every test: the renderer teardown (including the
    # lifecycle soak and the constructor-throw path) is what this stage exists
    # to prove, and the Mesa ICD frees its globals on this machine.
    ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
    UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
        ctest --test-dir "$VK_SANITIZE_DIR" --output-on-failure
fi

if [ "$VK_AUDIT_SOAK" != 0 ]; then
    echo "== vk_canvas: soak ($VK_AUDIT_SOAK repeats)"
    ctest --test-dir "$VK_BUILD_DIR" \
        -R 'renderer_lifecycle_test|gpu_smoke_test' \
        --repeat until-fail:"$VK_AUDIT_SOAK" --output-on-failure
fi

if [ "$VK_AUDIT_FUZZ_SECONDS" != 0 ]; then
    echo "== vk_canvas: fuzz ($VK_AUDIT_FUZZ_SECONDS s per target)"
    if ! command -v clang++ >/dev/null 2>&1; then
        echo "   clang++ not found; skipping"
    else
        cmake -S "$VK_DIR/dev" -B "$VK_FUZZ_DIR" -G Ninja \
            -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_CXX_COMPILER=clang++ \
            -DVCE_DEV_BUILD_FUZZ=ON \
            -DVCE_DEV_BUILD_SHADERS=OFF \
            -DVCE_DEV_BUILD_PLATFORM=OFF \
            -DVCE_DEV_BUILD_CAPTURE=OFF
        cmake --build "$VK_FUZZ_DIR" --target utf8_fuzz text_buffer_fuzz html_strip_fuzz
        for f in utf8_fuzz text_buffer_fuzz html_strip_fuzz; do
            echo "   $f"
            "$VK_FUZZ_DIR/fuzz/$f" -max_total_time="$VK_AUDIT_FUZZ_SECONDS" \
                -print_final_stats=1 -rss_limit_mb=2048
        done
    fi
fi

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

    if [ "$VK_AUDIT_FUZZ_SECONDS" != 0 ] && command -v clang++ >/dev/null 2>&1; then
        echo "== app_shell: fuzz ($VK_AUDIT_FUZZ_SECONDS s)"
        APP_SHELL_FUZZ_DIR="${APP_SHELL_FUZZ_DIR:-${APP_SHELL_BUILD_DIR}-fuzz}"
        cmake -S "$APP_SHELL_DIR/dev" -B "$APP_SHELL_FUZZ_DIR" -G Ninja \
            -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_CXX_COMPILER=clang++ \
            -DAPP_SHELL_DEV_BUILD_FUZZ=ON
        cmake --build "$APP_SHELL_FUZZ_DIR" --target utf16_fuzz
        "$APP_SHELL_FUZZ_DIR/fuzz/utf16_fuzz" \
            -max_total_time="$VK_AUDIT_FUZZ_SECONDS" -print_final_stats=1 -rss_limit_mb=2048
    fi
else
    echo "== app_shell: not present, skipping"
fi

echo "audit: ok"
