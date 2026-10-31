# libFuzzer harness for the JNI text seam's converter. Opt-in:
#
#   cmake -S dev -B <dir> -G Ninja -DCMAKE_CXX_COMPILER=clang++ \
#         -DAPP_SHELL_DEV_BUILD_FUZZ=ON
#   cmake --build <dir> --target utf16_fuzz
#   <dir>/fuzz/utf16_fuzz -max_total_time=30

if(NOT APP_SHELL_DEV_BUILD_FUZZ)
    return()
endif()

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(STATUS "APP_SHELL_DEV_BUILD_FUZZ: clang is required for libFuzzer; skipping")
    return()
endif()

set(_fuzz_flags -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)

add_executable(utf16_fuzz utf16_fuzz.cc)
target_compile_features(utf16_fuzz PRIVATE cxx_std_17)
target_compile_options(utf16_fuzz PRIVATE ${_fuzz_flags})
target_link_options(utf16_fuzz PRIVATE ${_fuzz_flags})
target_include_directories(utf16_fuzz PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_sources(utf16_fuzz PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../utf16_utf8.cc)
