# Android compilation coverage

The **Android device compilation** workflow uses the official NDK
`29.0.14206865` (r29), `arm64-v8a`, API 28, and shared libc++. It compiles real
production JNI platform callbacks, the RT64 Android window adapter, and the
mobile touch UI without requiring ROMs or generated retail CPU payloads.

The document-picker, portable-path, and support modules also link into a shared
validation library with `--no-undefined`, alongside pinned SDL2 with the staged
Android surface-lifecycle patch. No guest, renderer, or application entrypoints
are replaced by test stubs. The complete application is not linked by this check.

`scripts/verify_android_compile.py` checks all six first-party objects are ELF64
AArch64. It verifies both linked libraries have Android identity notes with API
28 or newer and the pinned r29 build, and every `LOAD` segment has at least 16 KB
alignment. The first-party library must depend on both `libc++_shared.so` and
`libSDL2.so`. Compiler commands, NDK provenance, and the verification report are
retained as the `android-device-compilation` artifact.

This covers native compilation and a production platform subset's linkage. It
does not build an APK, compile renderer shaders, validate Java/JNI integration on
a device, run gameplay, or prove Android 16 KB device behavior. Host tests and
actual-device acceptance remain separate checks.

## Run locally

Install [NDK r29](https://github.com/android/ndk/releases/tag/r29), CMake, and Ninja.
With Android command-line tools, the exact SDK Manager package is
`ndk;29.0.14206865`. Prepare dependencies through the regular workflow:

```sh
python3 scripts/bootstrap_dependencies.py --only n64-modern-runtime --only rt64 --only sdl2
python3 scripts/apply_dependency_patches.py
cmake -S runtime-recomp/tests/android_compile -B build/android-compile -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_HOME/ndk/29.0.14206865/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 \
  -DANDROID_STL=c++_shared -DCMAKE_BUILD_TYPE=Release
cmake --build build/android-compile --target DKRAndroidCompileCheck --parallel 3
python3 scripts/verify_android_compile.py build/android-compile \
  --readelf "$ANDROID_HOME/ndk/29.0.14206865/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf"
```

On macOS, use the official Darwin NDK package and its `darwin-x86_64` toolchain
directory. For shared prepared dependencies, pass
`-DDKR_DEPENDENCIES_DIR=/absolute/path/to/extern`. SDL's Android source patch is
staged inside the build directory; the source dependency stays unchanged.
Host renderer shader tools are required only when the renderer is enabled, not
for this ROM-free platform check.
