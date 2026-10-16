#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
task="${DKR_UPDATER_TASK_ROOT:?Set the qualification log directory}"
pipeline="${DKR_UPDATER_PIPELINE:?Set the reviewed patch-pipeline directory}"
build="${DKR_LINUX_BUILD_DIR:?Set the configured native Linux build directory}"
version="${DKR_RELEASE_VERSION:-$(tr -d '\r\n' < "$project_root/VERSION")}"
mkdir -p "$task"
export DKR_REVIEWED_GENERATED_V77="$pipeline/generated-v77"
export DKR_REVIEWED_GENERATED_V80="$pipeline/generated-v80"
cmake -S "$project_root/runtime-recomp" -B "$build" -DDKR_RELEASE_VERSION="$version" \
  -DDKR_GENERATED_SOURCE_V77="$pipeline/generated-v77" -DDKR_GENERATED_SOURCE_V80="$pipeline/generated-v80" \
  -DDKR_EXPERIMENTAL_OWNED_GENERATED="$pipeline/owned-v77" -DDKR_EXPERIMENTAL_OWNED_GENERATED_V80="$pipeline/owned-v80" \
  -DDKR_EXPERIMENTAL_RACE_TEST=ON -DDKR_REPLAY_QUALIFICATION=OFF -DDKR_LEGACY_QUALIFICATION=OFF \
  -DDKR_ANDROID_RENDER_QUALIFICATION=OFF -DDKR_EXPERIMENTAL_RENDER_QUALIFICATION=OFF \
  -DDKR_WATER_QUALIFICATION=OFF -DDKR_TASK_QUALIFICATION=OFF > "$task/Linux-configure.log" 2>&1
cmake --build "$build" --target DKRPortGame DKRLegacyModWorker DKRUpdateHelper DKRUpdateCoreTests DKRUpdateServiceTests DKRUpdateProcessTests \
  --parallel 2 > "$task/Linux-build.log" 2>&1
ctest --test-dir "$build" -R DKRUpdate --output-on-failure > "$task/Linux-update-tests.log" 2>&1
