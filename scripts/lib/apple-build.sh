#!/usr/bin/env bash
# Source-only Apple build helpers. Compatible with macOS Bash 3.2.

dkr_require_macos() {
  [[ "$(uname -s)" == Darwin ]] || {
    echo 'Apple builds require macOS.' >&2
    return 1
  }
}

dkr_require_tools() {
  local tool
  for tool in "$@"; do
    command -v "$tool" >/dev/null 2>&1 || {
      echo "Required build tool is missing from PATH: ${tool}" >&2
      return 1
    }
  done
}

dkr_require_xcode() {
  xcodebuild -version >/dev/null 2>&1 || {
    echo 'Install Xcode and select it with sudo xcode-select --switch /Applications/Xcode.app/Contents/Developer' >&2
    return 1
  }
  xcodebuild -checkFirstLaunchStatus >/dev/null 2>&1 || {
    echo 'Run sudo xcodebuild -runFirstLaunch' >&2
    return 1
  }
}

# Print the selected job count for assignment by the caller. An explicit
# value wins over CMake's environment setting, then the host CPU count.
dkr_apple_build_jobs() {
  local jobs="${1:-${CMAKE_BUILD_PARALLEL_LEVEL:-}}"
  if [[ -z "$jobs" ]]; then
    jobs="$(sysctl -n hw.logicalcpu 2>/dev/null)" || jobs=1
  fi
  [[ "$jobs" =~ ^[1-9][0-9]*$ ]] || {
    echo '--jobs / CMAKE_BUILD_PARALLEL_LEVEL must be a positive integer.' >&2
    return 1
  }
  printf '%s\n' "$jobs"
}
