#!/usr/bin/env bash
# Build a sideloadable DKR-R iOS app: one arm64 IPA for iOS 18 or newer.
#
# The IPA is ad-hoc signed. AltStore Classic, SideStore, or Sideloadly
# re-signs it with your Apple ID. Free Apple IDs cannot grant special
# entitlements, so this package has none (no JIT, no increased memory).
#
# Install:
#   Sideloadly: drop dist/DKR-R-<version>-iOS-arm64.ipa onto the window,
#   sign in with an Apple ID, and install to a connected iPhone or iPad.
#   AltStore Classic or SideStore: My Apps, plus, then the IPA.
#   Refresh the install before the 7-day signature expires.
#
# Bring your own ROM. Nothing from Nintendo or Rare is in this package.
# In the Files app, put an unzipped .z64, .n64 or .v64 directly in On My
# iPhone > DKR-R (not the DKR-R folder inside it, which holds settings), or
# share the file into DKR-R. Saves stay there.
# The app is landscape, fullscreen, and the silent switch does not mute it.
#
# The binary links N64ModernRuntime (GPL-3.0). This IPA is a GPL-3.0
# distribution, not an MIT-only app.
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "${project_root}/scripts/lib/apple-build.sh"
dkr_require_macos
dkr_require_tools python3 git cmake ninja xcrun codesign
dkr_require_xcode

if [[ "${DKR_MACOS_LOCK_ENTRY:-}" != "${BASH_SOURCE[0]}" ]]; then
  exec python3 "${project_root}/scripts/macos_build_lock.py" "${project_root}" "${BASH_SOURCE[0]}" "$@"
fi
unset DKR_MACOS_LOCK_ENTRY

jobs="$(dkr_apple_build_jobs)"
version="${DKR_RELEASE_VERSION:-$(tr -d '\r\n' <"${project_root}/VERSION")}"
generated_v77="${DKR_IOS_V77_GENERATED_SOURCE:-${project_root}/build/generated/v77/RecompiledFuncs}"
generated_v80="${DKR_IOS_V80_GENERATED_SOURCE:-${project_root}/build/generated/v80/RecompiledFuncs}"
host_dir="${project_root}/build/ios-host-tools"
build_dir="${DKR_IOS_BUILD_DIR:-${project_root}/build/dkr-runtime-ios}"

[[ "$version" =~ ^([1-9][0-9]{0,3})\.(0|[1-9][0-9]?)\.(0|[1-9][0-9]?)(-rc[1-9][0-9]*)?$ ]] || {
  echo "iOS packaging needs major.minor.patch (optionally -rcN), with major 1–9999 and minor/patch 0–99 (got ${version})." >&2
  exit 1
}
[[ -d "$generated_v77" && -d "$generated_v80" ]] || {
  echo "Generated CPU payloads are missing. Expected ${generated_v77} and ${generated_v80}." >&2
  exit 1
}

xcrun --sdk iphoneos --show-sdk-path >/dev/null || {
  echo "The iPhone SDK is unavailable. Select a full Xcode installation with xcode-select." >&2
  exit 1
}
for tool in metal metallib actool ibtool; do
  xcrun --sdk iphoneos --find "$tool" >/dev/null || {
    echo "Xcode tool ${tool} is unavailable. Install the iOS and Metal toolchain components in Xcode." >&2
    exit 1
  }
done
xcrun --sdk iphoneos metal --version >/dev/null || {
  echo "The Metal compiler cannot run. Install its toolchain with: xcodebuild -downloadComponent MetalToolchain" >&2
  exit 1
}

if [[ ! -e "${project_root}/extern/sdl2/.git" ]]; then
  python3 "${project_root}/scripts/bootstrap_dependencies.py" --only sdl2
fi
python3 "${project_root}/scripts/apply_dependency_patches.py" --root "${project_root}"
# RT64 and the runtime include metal-cpp from src/contrib/metal-cpp. The
# macOS prepare script links it too, but prepared payloads may come from
# elsewhere through the DKR_IOS_*_GENERATED_SOURCE overrides.
rt64="${project_root}/extern/rt64"
if [[ -d "${rt64}/src/contrib/plume/contrib/metal-cpp" && ! -e "${rt64}/src/contrib/metal-cpp" ]]; then
  ln -s plume/contrib/metal-cpp "${rt64}/src/contrib/metal-cpp"
fi

cmake -S "${project_root}/packaging/ios/host-tools" -B "${host_dir}" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "${host_dir}" --parallel "${jobs}"

file_to_c="${host_dir}/file_to_c"
spirv_cross_msl="${host_dir}/spirv_cross_msl"
[[ -x "$file_to_c" && -x "$spirv_cross_msl" ]] || {
  echo "Host shader tools were not produced in ${host_dir}." >&2
  exit 1
}

cmake -S "${project_root}/runtime-recomp" -B "${build_dir}" -G Ninja \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_SYSTEM_PROCESSOR=arm64 \
  -DCMAKE_OSX_SYSROOT=iphoneos \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=18.0 \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF \
  -DDKR_RUNTIME_BUILD_GENERATED=ON \
  -DDKR_RUNTIME_BUILD_RT64=ON \
  -DDKR_RUNTIME_BUILD_SDL3_INPUT_HOST=OFF \
  -DDKRPORT_ROOT="${project_root}" \
  -DDKR_RELEASE_VERSION="${version}" \
  -DDKR_GENERATED_SOURCE_V77="${generated_v77}" \
  -DDKR_GENERATED_SOURCE_V80="${generated_v80}" \
  -DDKR_HOST_FILE_TO_C="${file_to_c}" \
  -DDKR_HOST_SPIRV_CROSS_MSL="${spirv_cross_msl}"
cmake --build "${build_dir}" --parallel "${jobs}" --target DKRPortGame

python3 "${project_root}/scripts/package_ios.py" "${build_dir}" "${version}"
