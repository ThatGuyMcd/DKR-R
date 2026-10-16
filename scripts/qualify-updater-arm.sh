#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
task="${DKR_UPDATER_TASK_ROOT:?Set the qualification log directory}"
build="${DKR_LINUX_BUILD_DIR:?Set the configured ARM64 build directory}"
pipeline="${DKR_UPDATER_PIPELINE:?Set the reviewed patch-pipeline directory}"
sysroot="${DKR_ARM64_SYSROOT:?Set the ARM64 sysroot}"
version="${DKR_RELEASE_VERSION:-$(tr -d '\r\n' < "$project_root/VERSION")}"
mkdir -p "$task"
export QEMU_LD_PREFIX="$sysroot" PKG_CONFIG_SYSROOT_DIR="$sysroot"
export PKG_CONFIG_LIBDIR="$sysroot/usr/lib/aarch64-linux-gnu/pkgconfig:$sysroot/usr/share/pkgconfig"
export DKR_REVIEWED_GENERATED_V77="$pipeline/generated-v77" DKR_REVIEWED_GENERATED_V80="$pipeline/generated-v80"
cmake -S "$project_root/runtime-recomp" -B "$build" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$project_root/runtime-recomp/cmake/LinuxArm64.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DDKR_RELEASE_VERSION="$version" \
  -DDKR_ARM64_SYSROOT="$sysroot" -DCMAKE_FIND_ROOT_PATH="$sysroot/usr;/usr/aarch64-linux-gnu" \
  -DSDL2_DIR="$sysroot/usr/lib/aarch64-linux-gnu/cmake/SDL2" \
  -DCMAKE_EXE_LINKER_FLAGS="-Wl,-rpath-link,$sysroot/usr/lib/aarch64-linux-gnu" \
  -DCURL_LIBRARY_RELEASE="$sysroot/usr/lib/aarch64-linux-gnu/libcurl.so" \
  -DCURL_INCLUDE_DIR="$sysroot/usr/include/aarch64-linux-gnu" \
  -DDKR_GENERATED_SOURCE_V77="$pipeline/generated-v77" -DDKR_GENERATED_SOURCE_V80="$pipeline/generated-v80" \
  -DDKR_EXPERIMENTAL_OWNED_GENERATED="$pipeline/owned-v77" -DDKR_EXPERIMENTAL_OWNED_GENERATED_V80="$pipeline/owned-v80" \
  -DDKR_EXPERIMENTAL_RACE_TEST=ON -DDKR_LEGACY_QUALIFICATION=OFF -DDKR_REPLAY_QUALIFICATION=OFF \
  -DDKR_ANDROID_RENDER_QUALIFICATION=OFF -DDKR_EXPERIMENTAL_RENDER_QUALIFICATION=OFF \
  -DDKR_WATER_QUALIFICATION=OFF -DDKR_TASK_QUALIFICATION=OFF > "$task/ARM-configure.log" 2>&1
cmake --build "$build" --target DKRPortGame DKRUpdateHelper DKRUpdateCoreTests DKRUpdateServiceTests DKRUpdateProcessTests \
  --parallel 2 > "$task/ARM-build.log" 2>&1
ctest --test-dir "$build" -R DKRUpdate --output-on-failure > "$task/ARM-update-tests.log" 2>&1
