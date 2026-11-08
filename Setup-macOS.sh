#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
skip_deps=0
skip_hooks=0
for argument in "$@"; do
  case "$argument" in
  --skip-deps) skip_deps=1 ;;
  --skip-hooks) skip_hooks=1 ;;
  -h | --help)
    echo 'Usage: ./Setup-macOS.sh [--skip-deps] [--skip-hooks] (install prerequisites, or only check them; --skip-hooks leaves git hooks alone)'
    exit 0
    ;;
  *)
    echo "Unknown option: $argument" >&2
    exit 1
    ;;
  esac
done
source "$project_root/scripts/macos-host-tools.sh"
dkr_macos_host_tools "$skip_deps"
# Never fails Setup: bootstrap_hooks.py reports problems and exits 0.
if [[ "$skip_hooks" -eq 0 && -f "$project_root/scripts/bootstrap_hooks.py" ]]; then
  python3 "$project_root/scripts/bootstrap_hooks.py" || true
fi
echo 'macOS prerequisites passed (same checks as Build preparation). Run ./Build-macOS.sh --rom-v77 "/path/to/US 1.0.z64" --rom-v80 "/path/to/US Rev A.z64".'
