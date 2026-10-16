#!/usr/bin/env bash
set -euo pipefail

missing=0
for tool in cmake ninja c++ git pkg-config python3; do
  if ! command -v "${tool}" >/dev/null 2>&1; then
    echo "Missing required tool: ${tool}" >&2
    missing=1
  fi
done
if command -v pkg-config >/dev/null 2>&1; then
  for package in sdl2 gtk+-3.0 vulkan; do
    if ! pkg-config --exists "${package}"; then
      echo "Missing development package exposed by pkg-config: ${package}" >&2
      missing=1
    fi
  done
fi
if [[ "${missing}" -ne 0 ]]; then
  cat >&2 <<'MSG'
Ubuntu 24.04 build prerequisites:
  sudo apt install build-essential cmake ninja-build git pkg-config python3 \
    libsdl2-dev libgtk-3-dev libvulkan-dev vulkan-tools \
    libasound2-dev libpulse-dev libsamplerate0-dev \
    libx11-dev libxext-dev libxrandr-dev libwayland-dev libxkbcommon-dev
Install the missing prerequisites, then rerun ./Setup-Linux.sh.
MSG
  exit 1
fi

cmake --version | sed -n '1p'
c++ --version | sed -n '1p'
pkg-config --modversion sdl2 gtk+-3.0 vulkan
echo 'Linux runtime prerequisites passed. Prepare generated code, then run ./Build-Linux.sh.'
