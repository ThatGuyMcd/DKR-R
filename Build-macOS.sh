#!/usr/bin/env bash
# One-command macOS build: prepares dependencies and both recompiled
# payloads from your ROMs, then builds and tests the native executable.
#   ./Build-macOS.sh --rom-v77 <US 1.0 ROM> --rom-v80 <US Rev A ROM>
# Prebuilt payloads can be supplied instead with DKR_MAC_GENERATED_SOURCE and
# DKR_MAC_V80_GENERATED_SOURCE. Prepare options (--skip-deps, --force,
# --jobs) are passed through.
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${project_root}/scripts/lib/apple-build.sh"
# Inherit the same lock when Build invokes preparation; release on process exit.
if [[ "${DKR_MACOS_LOCK_ENTRY:-}" != "${BASH_SOURCE[0]}" ]]; then
  exec python3 "${project_root}/scripts/macos_build_lock.py" "${project_root}" "${BASH_SOURCE[0]}" "$@"
fi
unset DKR_MACOS_LOCK_ENTRY
prepare_args=()
jobs="${CMAKE_BUILD_PARALLEL_LEVEL:-}"
package=0
notarize=0
while [[ $# -gt 0 ]]; do
  case "$1" in
  --rom-v77 | --rom-v80 | --jobs)
    [[ $# -ge 2 && "$2" != --* && -n "$2" ]] || {
      echo "$1 requires a value." >&2
      exit 1
    }
    if [[ "$1" == --jobs ]]; then jobs="$2"; fi
    prepare_args+=("$1" "$2")
    shift
    ;;
  --skip-deps | --force) prepare_args+=("$1") ;;
  --package) package=1 ;;
  --notarize) package=1 notarize=1 ;;
  -h | --help)
    cat <<'EOF'
Usage: ./Build-macOS.sh --rom-v77 PATH --rom-v80 PATH [options]

Prepare, build and test DKR-R on macOS.
  --rom-v77 PATH   US v1.0 ROM (or DKR_ROM_V77)
  --rom-v80 PATH   US Rev A/v1.1 ROM (or DKR_ROM_V80)
  --jobs N        Limit parallel build jobs (default: host CPU count)
  --skip-deps     Skip Homebrew installation
  --force         Regenerate payloads even when inputs are unchanged
  --package       Also create a self-contained, ad-hoc-signed app ZIP in dist/
                  (runs on this Mac's macOS version or newer: Homebrew SDL sets the floor)
  --notarize      Package with a Developer ID signature, notarize and staple.
                  Needs DKR_MAC_SIGN_IDENTITY (a "Developer ID Application" identity)
                  and DKR_MAC_NOTARY_PROFILE (an xcrun notarytool keychain profile)
  -h, --help      Show this help

Prerequisites: Xcode and Homebrew. Quote ROM paths with spaces.
Output: build/dkr-runtime-macos/bin/Release/DKR-R.
Launch with ./Run-macOS.sh.

Advanced: DKR_MAC_BUILD_DIR overrides the build directory; DKR_GENERATED_ROOT
overrides the payload directory. Set both DKR_MAC_GENERATED_SOURCE and
DKR_MAC_V80_GENERATED_SOURCE to use existing payloads and dependencies.
EOF
    exit 0
    ;;
  *)
    echo "Unknown option: $1" >&2
    exit 1
    ;;
  esac
  shift
done
# Prints the hook install command if the hooks are missing; never fails Build.
if [[ -f "${project_root}/scripts/bootstrap_hooks.py" ]]; then python3 "${project_root}/scripts/bootstrap_hooks.py" --check || true; fi
jobs="$(dkr_apple_build_jobs "$jobs")"
export CMAKE_BUILD_PARALLEL_LEVEL="$jobs"
dkr_require_macos

if [[ -z "${DKR_MAC_GENERATED_SOURCE:-}" || -z "${DKR_MAC_V80_GENERATED_SOURCE:-}" ]]; then
  # Apple's Bash 3.2 treats an empty array as unset under nounset.
  if [[ ${#prepare_args[@]} -gt 0 ]]; then
    "${project_root}/scripts/Prepare-DKR-Runtime-macOS.sh" "${prepare_args[@]}"
  else
    "${project_root}/scripts/Prepare-DKR-Runtime-macOS.sh"
  fi
  # It finishes with scripts/prepare_payloads.py; CMake defaults the owned
  # engine and kernels to the same directory.
  DKR_MAC_GENERATED_SOURCE="${project_root}/build/prepared/generated-v77"
  DKR_MAC_V80_GENERATED_SOURCE="${project_root}/build/prepared/generated-v80"
fi

dkr_require_tools cmake ninja
for dependency in n64-modern-runtime rt64; do
  [[ -d "${project_root}/extern/${dependency}" ]] || {
    echo "extern/${dependency} is missing; run scripts/Prepare-DKR-Runtime-macOS.sh first." >&2
    exit 1
  }
done

generated_source="${DKR_MAC_GENERATED_SOURCE}"
[[ "$generated_source" == /* ]] || generated_source="$PWD/$generated_source"
[[ -d "${generated_source}" ]] || {
  echo "US v1.0/v77 Patch Pipeline output is missing: ${generated_source}" >&2
  exit 1
}

version="${DKR_RELEASE_VERSION:-$(tr -d '\r\n' <"${project_root}/VERSION")}"
[[ "$version" =~ ^[A-Za-z0-9][A-Za-z0-9._+-]*$ ]] || {
  echo 'Invalid release version.' >&2
  exit 1
}
host_arch="$(uname -m)"
architectures="${DKR_MAC_ARCHITECTURES:-${host_arch}}"
[[ "$architectures" =~ ^(arm64|x86_64)(\;(arm64|x86_64))*$ ]] || {
  echo 'Architectures must be arm64, x86_64, or a semicolon-separated list.' >&2
  exit 1
}
if [[ "$package" -eq 1 ]]; then
  [[ "$architectures" != *';'* ]] || {
    echo '--package supports a single architecture.' >&2
    exit 1
  }
  [[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+ ]] || {
    echo '--package needs a release version starting with major.minor.patch (e.g. 1.0.5).' >&2
    exit 1
  }
fi
if [[ "$notarize" -eq 1 ]]; then
  # Fail before the build rather than after it.
  [[ -n "${DKR_MAC_SIGN_IDENTITY:-}" && -n "${DKR_MAC_NOTARY_PROFILE:-}" ]] || {
    echo '--notarize needs DKR_MAC_SIGN_IDENTITY and DKR_MAC_NOTARY_PROFILE.' >&2
    exit 1
  }
fi
build_dir="${DKR_MAC_BUILD_DIR:-${project_root}/build/dkr-runtime-macos}"
revision_80_generated="${DKR_MAC_V80_GENERATED_SOURCE}"
[[ "$revision_80_generated" == /* ]] || revision_80_generated="$PWD/$revision_80_generated"

[[ -d "${revision_80_generated}" ]] || {
  echo 'Set DKR_MAC_V80_GENERATED_SOURCE to Patch Pipeline output generated from the matching US Rev A/v1.1 ELF.' >&2
  exit 1
}

# prepare_payloads.py names its configs v<rev>.toml; N64Recomp's sit beside
# RecompiledFuncs as dkr.us.v<rev>.toml.
code_policy() {
  local parent
  parent="$(dirname "$1")"
  if [[ -f "${parent}/v$2.toml" ]]; then echo "${parent}/v$2.toml"; else echo "${parent}/dkr.us.v$2.toml"; fi
}

cmake -S "${project_root}/runtime-recomp" -B "${build_dir}" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_DEPLOYMENT_TARGET="${DKR_MACOS_DEPLOYMENT_TARGET:-12.0}" \
  -DCMAKE_OSX_ARCHITECTURES="${architectures}" \
  -DDKRPORT_ROOT="${project_root}" \
  -DDKR_RELEASE_VERSION="${version}" \
  -DDKR_RUNTIME_BUILD_GENERATED=ON \
  -DDKR_RUNTIME_BUILD_RT64=ON \
  -DDKR_GENERATED_SOURCE_V77="${generated_source}" \
  -DDKR_GENERATED_SOURCE_V80="${revision_80_generated}" \
  -DDKR_CODE_POLICY_V77="$(code_policy "${generated_source}" 77)" \
  -DDKR_CODE_POLICY_V80="$(code_policy "${revision_80_generated}" 80)"
cmake --build "${build_dir}" --parallel "${jobs}"
ctest --test-dir "${build_dir}" --output-on-failure --no-tests=error -R '^DKR'

executable="${build_dir}/bin/Release/DKR-R"
[[ -x "${executable}" ]] || {
  echo "The macOS executable was not produced at ${executable}." >&2
  exit 1
}

pak_test="$(mktemp -d "${TMPDIR:-/tmp}/dkr-r-pak-test.XXXXXX")"
cleanup() { rm -rf -- "${pak_test}"; }
trap cleanup EXIT
"${executable}" --self-test-pak "${pak_test}"
printf '\nBuild and tests passed. Launch with ./Run-macOS.sh.\n'
if [[ "$package" -eq 1 ]]; then
  package_args=("${build_dir}" "${version}")
  [[ "$notarize" -eq 0 ]] || package_args+=(--notarize)
  python3 "${project_root}/scripts/package_macos.py" "${package_args[@]}"
fi
