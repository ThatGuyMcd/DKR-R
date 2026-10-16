#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exe="${DKR_LINUX_BUILD_DIR:-${project_root}/build/dkr-runtime-linux}/bin/Release/DKR-R"
[[ -x "${exe}" ]] || {
  echo 'Linux runtime missing. Run ./Setup-Linux.sh and ./Build-Linux.sh first.' >&2
  exit 1
}
exec "${exe}" "$@"
