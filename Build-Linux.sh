#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${DKR_LINUX_BUILD_DIR:-${project_root}/build/dkr-runtime-linux}"
revision_80_generated="${DKR_LINUX_V80_GENERATED_SOURCE:-}"
generated_source="${DKR_LINUX_GENERATED_SOURCE:-${project_root}/runtime-recomp/RecompiledFuncs}"
version="${DKR_RELEASE_VERSION:-$(tr -d '\r\n' <"${project_root}/VERSION")}"

package=0
usage() {
  cat <<'HELP'
Usage: ./Build-Linux.sh [--package]
Build and test the prepared Linux runtime. Add --package to create an AppImage.

Inputs:
  DKR_LINUX_GENERATED_SOURCE      Prepared US v1.0/v77 source directory
  DKR_LINUX_V80_GENERATED_SOURCE  Prepared US Rev A/v80 source directory
  DKR_LINUX_BUILD_DIR             Build directory (default: build/dkr-runtime-linux)
  CMAKE_BUILD_PARALLEL_LEVEL      Optional parallel build job count

Run ./Setup-Linux.sh to check prerequisites. Packaging also needs linuxdeploy
and its AppImage plugin; see docs/BUILDING.md.
HELP
}
for argument in "$@"; do
  case "$argument" in
  --package) package=1 ;;
  -h | --help)
    usage
    exit 0
    ;;
  *)
    echo "Unknown option: $argument" >&2
    usage >&2
    exit 2
    ;;
  esac
done

[[ -d "${generated_source}" ]] || {
  echo "US v1.0/v77 Patch Pipeline output is missing: ${generated_source}" >&2
  exit 1
}
[[ -n "${revision_80_generated}" && -d "${revision_80_generated}" ]] || {
  echo 'Set DKR_LINUX_V80_GENERATED_SOURCE to the US Rev A/v1.1 Patch Pipeline output.' >&2
  exit 1
}
[[ -d "${project_root}/extern/n64-modern-runtime" ]] || {
  echo 'N64ModernRuntime is missing. Run the dependency preparation first.' >&2
  exit 1
}
[[ -d "${project_root}/extern/rt64" ]] || {
  echo 'RT64 is missing. Run the renderer-enabled dependency preparation first.' >&2
  exit 1
}

cmake -S "${project_root}/runtime-recomp" -B "${build_dir}" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON \
  -DDKRPORT_ROOT="${project_root}" \
  -DDKR_RELEASE_VERSION="${version}" \
  -DDKR_RUNTIME_BUILD_GENERATED=ON \
  -DDKR_RUNTIME_BUILD_RT64=ON \
  -DDKR_RUNTIME_BUILD_SDL3_INPUT_HOST=ON \
  -DDKR_GENERATED_SOURCE_V77="${generated_source}" \
  -DDKR_GENERATED_SOURCE_V80="${revision_80_generated}"
cmake --build "${build_dir}" --parallel
ctest --test-dir "${build_dir}" --no-tests=error --output-on-failure -R '^DKR'

binary="${build_dir}/bin/Release/DKR-R"
[[ -x "${binary}" ]] || {
  echo "Missing Linux runtime after build: ${binary}" >&2
  exit 1
}
pak_test="$(mktemp -d "${TMPDIR:-/tmp}/dkr-port-pak-test.XXXXXX")"
trap 'rm -rf -- "${pak_test}"' EXIT
"${binary}" --self-test-pak "${pak_test}"

printf '\nLinux runtime and tests passed.\n'
printf 'Runtime: %s\n' "${binary}"
if [[ "${package}" == 1 ]]; then
  DKR_RELEASE_VERSION="${version}" \
    DKR_LINUX_BUILD_DIR="${build_dir}" \
    "${project_root}/scripts/Package-Linux-AppImage.sh"
  printf 'AppImage: %s\n' "${project_root}/dist/DKR-R-${version}-Linux-x86_64.AppImage"
fi
