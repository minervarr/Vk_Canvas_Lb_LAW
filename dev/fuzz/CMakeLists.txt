# libFuzzer harnesses for the pure parsers. Opt-in:
#
#   cmake -S dev -B <dir> -G Ninja -DCMAKE_CXX_COMPILER=clang++ \
#         -DVCE_DEV_BUILD_FUZZ=ON -DCMAKE_BUILD_TYPE=Debug
#   cmake --build <dir> --target utf8_fuzz text_buffer_fuzz html_strip_fuzz utf16_fuzz
#   <dir>/dev/fuzz/utf8_fuzz -max_total_time=30
#
# These are not CTest entries: libFuzzer owns the loop. tools/audit.sh runs
# them for VK_AUDIT_FUZZ_SECONDS when a clang with libFuzzer is available.

if(NOT VCE_DEV_BUILD_FUZZ)
    return()
endif()

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(STATUS "VCE_DEV_BUILD_FUZZ: clang is required for libFuzzer; skipping")
    return()
endif()

set(_fuzz_flags -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)

foreach(_fuzz utf8_fuzz text_buffer_fuzz html_strip_fuzz)
    add_executable(${_fuzz} ${_fuzz}.cc)
    target_compile_features(${_fuzz} PRIVATE cxx_std_17)
    target_compile_options(${_fuzz} PRIVATE ${_fuzz_flags})
    target_link_options(${_fuzz} PRIVATE ${_fuzz_flags})
endforeach()

# utf8.hh is part of the font-engine submodule; vk_canvas_draw re-exports its
# include directory. No symbol from the target is needed.
target_link_libraries(utf8_fuzz PRIVATE vk_canvas_draw)
target_link_libraries(text_buffer_fuzz PRIVATE vk_canvas_base)
target_link_libraries(html_strip_fuzz PRIVATE vk_canvas_draw)

# app_shell's JNI UTF-16 seam. The harness stays under app_shell/dev/fuzz so the
# standalone app_shell dev tree keeps the same file; compiling it here is what
# lets one audit command run every target. This configure sets
# VCE_DEV_BUILD_APP_SHELL=OFF, so only the source and the converter are needed.
add_executable(utf16_fuzz ${VK_CANVAS_DIR}/app_shell/dev/fuzz/utf16_fuzz.cc)
target_compile_features(utf16_fuzz PRIVATE cxx_std_17)
target_compile_options(utf16_fuzz PRIVATE ${_fuzz_flags})
target_link_options(utf16_fuzz PRIVATE ${_fuzz_flags})
target_include_directories(utf16_fuzz PRIVATE ${VK_CANVAS_DIR}/app_shell)
target_sources(utf16_fuzz PRIVATE ${VK_CANVAS_DIR}/app_shell/utf16_utf8.cc)
