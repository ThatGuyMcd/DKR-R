# iOS compilation coverage

The **iOS device compilation** workflow builds real first-party sources against
the iPhoneOS SDK on a macOS runner. It needs no ROM, generated retail CPU code,
signing identity, or Metal shader compiler. It compiles:

- The UIKit lifecycle, file import, audio-session, and platform adapter.
- The UIKit/Metal layer adapter and native document picker.
- The production mobile touch UI, portable-path, and support modules.
- Pinned SDL2, including the reviewed UIScene lifecycle patch.

The build produces a static validation library. A separate verification step
checks each platform object's Mach-O architecture and build-version metadata:
`arm64`, device platform `IOS`, and minimum deployment version `18.0`. Host or
simulator objects fail validation. CI retains the SDK/toolchain versions, compiler
commands, and object verification report as the `ios-device-compilation` artifact.

This establishes compilation coverage, not full application linking, renderer
shader compilation, gameplay, signing, IPA installation, or device behavior. The
existing iOS packaging tests run separately in the host test suite. A complete
sideload build still uses `scripts/build-ios.sh` with prepared CPU payloads and
native host shader tools; device acceptance remains required.

## Run locally

Use full Xcode with the iPhoneOS SDK, CMake, and Ninja. Prepare the pinned `sdl2`
and recursive `rt64` dependencies and apply the reviewed patches using the normal
bootstrap commands. The latter supplies the exact ImGui and native-file-dialog
headers used by the app; this check does not compile RT64 itself.

```sh
python3 scripts/bootstrap_dependencies.py --only sdl2 --only rt64
python3 scripts/apply_dependency_patches.py
cmake -S runtime-recomp/tests/ios_compile -B build/ios-compile -G Ninja \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=18.0 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/ios-compile --target DKRIosCompileCheck --parallel 3
python3 scripts/verify_ios_compile.py build/ios-compile
```

For a worktree with already prepared dependencies, add
`-DDKR_DEPENDENCIES_DIR=/absolute/path/to/extern` to configuration. The build reads
that dependency tree and writes outputs only into its build directory. Prepare
patches in a separate checkout if the shared dependency tree must stay unchanged.
