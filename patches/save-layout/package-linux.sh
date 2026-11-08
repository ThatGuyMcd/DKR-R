#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
task_root="${1:?Supply a NEW pipeline task output root}"
export DKR_LINUX_BUILD_DIR=/home/admin/dkr-legacy-behaviour-20261010-x64
export DKR_RELEASE_VERSION=1.0.7+beta.5 DKR_LINUX_ARCH=x86_64
export DKR_SKIP_RUNTIME_TESTS=1 DKR_DEFER_RUNTIME_TESTS_TO_USER=1
export DKR_APPDIR=/home/admin/dkr-save-layout-beta5-20261017.AppDir
export DKR_APPIMAGE_OUTPUT="${task_root}/release/DKR-R-1.0.7+beta.5-Linux-x86_64.AppImage"
export DKR_CODE_EXAMPLE=/mnt/d/DiddykongWorkFolder/custom-magic-codes-20261015/release-107-beta2/dkr_custom_trophies-1.0.2.nrm
export DKR_CODE_DIFFICULTY_EXAMPLE=/mnt/d/DiddykongWorkFolder/custom-magic-codes-20261015/release-107-beta2/dkr_ai_boss_difficulty-1.0.0.nrm
[[ ! -e "${DKR_APPDIR}" && ! -e "${DKR_APPIMAGE_OUTPUT}" ]] || { echo 'Choose fresh packaging output.' >&2; exit 1; }
bash "${project_root}/scripts/Package-Linux-AppImage.sh"
install -m 0644 "${project_root}/patches/save-layout/README.md" "${task_root}/release/SAVE-LAYOUT-NOTES.md"
