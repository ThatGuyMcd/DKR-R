#!/usr/bin/env bash
# Shared prerequisite contract for Setup and runtime preparation.
source "${BASH_SOURCE[0]%/*}/lib/apple-build.sh"
dkr_macos_host_tools() {
  local skip_deps="$1" make_version probe
  dkr_require_macos || return 1
  # Command Line Tools are enough: RT64's Metal shaders compile at runtime.
  xcrun --sdk macosx --show-sdk-path >/dev/null 2>&1 || {
    echo 'Install the Command Line Tools: xcode-select --install' >&2
    return 1
  }
  if [[ "$skip_deps" -eq 0 ]]; then
    command -v brew >/dev/null || {
      echo 'Install Homebrew: https://brew.sh/' >&2
      return 1
    }
    # Install only what is missing: on macOS releases Homebrew no longer
    # bottles, reinstalling a present tool (python) means a failing source build.
    local pair missing=()
    for pair in cmake:cmake ninja:ninja make:gmake pcre2:pcre2-config sdl2:sdl2-config coreutils:sha1sum; do
      command -v "${pair#*:}" >/dev/null || missing+=("${pair%%:*}")
    done
    # tomllib needs 3.11; the Command Line Tools ship 3.9.
    python3 -c 'import sys; sys.exit(sys.version_info < (3, 11))' 2>/dev/null || missing+=(python)
    if [[ "${#missing[@]}" -gt 0 ]]; then
      HOMEBREW_NO_AUTO_UPDATE=1 HOMEBREW_NO_INSTALL_UPGRADE=1 brew install --quiet "${missing[@]}" || return 1
    fi
  fi
  dkr_require_tools git python3 cmake ninja c++ shasum sha1sum pcre2-config sdl2-config || {
    echo 'Run ./Setup-macOS.sh (sha1sum is supplied by brew install coreutils).' >&2
    return 1
  }
  GMAKE="$(command -v gmake || true)"
  if [[ -z "$GMAKE" ]] && command -v brew >/dev/null; then
    GMAKE="$(brew --prefix make)/bin/gmake"
  fi
  [[ -x "$GMAKE" ]] || {
    echo 'GNU Make 4 or newer is required: brew install make' >&2
    return 1
  }
  make_version="$("$GMAKE" --version)"
  [[ "$make_version" =~ GNU\ Make\ ([0-9]+) ]] && [[ "${BASH_REMATCH[1]}" -ge 4 ]] || {
    echo 'GNU Make 4 or newer is required: brew install make' >&2
    return 1
  }
  # Exercise the exact verifier interface before any ROM extraction.
  probe="$(mktemp -d)" || return 1
  printf abc >"$probe/input"
  printf 'a9993e364706816aba3e25717850c26c9cd0d89d  input\n' >"$probe/checksums"
  if ! (cd "$probe" && sha1sum -c --quiet checksums); then
    rm -rf -- "$probe"
    echo 'sha1sum must support -c --quiet. Install coreutils and put its bin directory on PATH.' >&2
    return 1
  fi
  rm -rf -- "$probe"
}
