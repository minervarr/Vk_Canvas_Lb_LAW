cmake -S . -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=/opt/android-ndk/build/cmake/android-legacy.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-24 \
  -DANDROID_STL=c++_shared \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DAPP_SHELL_NATIVE_APP_GLUE_DIR=/opt/android-ndk/sources/android/native_app_glue
