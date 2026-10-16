#!/usr/bin/env bash
# Cross-build host only. AppImage's AI marker does not match the host's
# binfmt_misc ELF padding mask; extract with QEMU, then use the registered
# ARM64 interpreter for the ordinary ELF files inside. Never needed on ARM.
set -euo pipefail
image="$(realpath "$1")"
shift
[[ "${1:-}" != --appimage-extract-and-run ]] || shift
if [[ "$(od -An -tx1 -j8 -N3 "${image}" | tr -d '[:space:]')" != 414902 ]]; then
  exec "${image}" "$@"
fi
temporary="$(mktemp -d "${TMPDIR:-/tmp}/dkr-arm-image.XXXXXX")"
trap 'rm -rf -- "${temporary}"' EXIT
(cd "${temporary}" && qemu-aarch64-static "${image}" --appimage-extract >/dev/null)
APPDIR="${temporary}/squashfs-root" APPIMAGE="${image}" "${temporary}/squashfs-root/AppRun" "$@"
