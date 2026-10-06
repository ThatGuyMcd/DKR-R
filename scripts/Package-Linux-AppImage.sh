#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIRECTORY="${DKR_LINUX_BUILD_DIR:-${PROJECT_ROOT}/build/dkr-runtime-linux}"
if [[ -f "${BUILD_DIRECTORY}/CMakeCache.txt" ]] &&
   grep -Eq '^DKR_EXPERIMENTAL_RENDER_QUALIFICATION:BOOL=(ON|1|TRUE|YES)[[:space:]]*$' "${BUILD_DIRECTORY}/CMakeCache.txt"; then
  echo 'Refusing to package private experimental GPU retirement qualification. Reconfigure and rebuild with DKR_EXPERIMENTAL_RENDER_QUALIFICATION=OFF.' >&2
  exit 1
fi
if [[ -f "${BUILD_DIRECTORY}/CMakeCache.txt" ]] &&
   grep -Eq '^DKR_REPLAY_QUALIFICATION:BOOL=(ON|1|TRUE|YES)[[:space:]]*$' "${BUILD_DIRECTORY}/CMakeCache.txt"; then
  echo 'Refusing to package private replay fixture capture.' >&2
  exit 1
fi
if [[ -f "${BUILD_DIRECTORY}/CMakeCache.txt" ]] &&
   grep -Eq '^DKR_ANDROID_RENDER_QUALIFICATION:BOOL=(ON|1|TRUE|YES)[[:space:]]*$' "${BUILD_DIRECTORY}/CMakeCache.txt"; then
  echo 'Refusing to package private Android renderer qualification.' >&2
  exit 1
fi
if [[ -f "${BUILD_DIRECTORY}/CMakeCache.txt" ]] &&
   grep -Eq '^DKR_WATER_QUALIFICATION:BOOL=(ON|1|TRUE|YES)[[:space:]]*$' "${BUILD_DIRECTORY}/CMakeCache.txt"; then
  echo 'Refusing to package private water-scene qualification.' >&2
  exit 1
fi
if [[ -f "${BUILD_DIRECTORY}/CMakeCache.txt" ]] &&
   grep -Eq '^DKR_TASK_QUALIFICATION:BOOL=(ON|1|TRUE|YES)[[:space:]]*$' "${BUILD_DIRECTORY}/CMakeCache.txt"; then
  echo 'Refusing to package private host-task fault injection. Rebuild with DKR_TASK_QUALIFICATION=OFF.' >&2
  exit 1
fi
if [[ -f "${BUILD_DIRECTORY}/CMakeCache.txt" ]] &&
   grep -Eq '^DKR_LEGACY_QUALIFICATION:BOOL=(ON|1|TRUE|YES)[[:space:]]*$' "${BUILD_DIRECTORY}/CMakeCache.txt"; then
  echo 'Refusing to package a private legacy-content qualification build. Reconfigure and rebuild with DKR_LEGACY_QUALIFICATION=OFF first.' >&2
  exit 1
fi
BINARY="${BUILD_DIRECTORY}/bin/Release/DKR-R"
if [[ -f "${BINARY}" ]] && grep -aFq 'DKR_REPLAY_CAPTURE_FILE' "${BINARY}"; then
  echo 'Release executable still contains private replay fixture capture.' >&2
  exit 1
fi
INPUT_HOST_DIRECTORY="${BUILD_DIRECTORY}/bin/Release/libexec/dkr-r"
INPUT_HOST="${INPUT_HOST_DIRECTORY}/DKR-R-InputHost"
MOD_WORKER="${INPUT_HOST_DIRECTORY}/DKR-R-ModWorker"
SDL3_LIBRARY="${INPUT_HOST_DIRECTORY}/libSDL3.so.0"
VERSION_FILE_VALUE="$(tr -d '\r\n' < "${PROJECT_ROOT}/VERSION")"
VERSION="${DKR_RELEASE_VERSION:-${VERSION_FILE_VALUE}}"
EXPERIMENTAL_RACE_TEST=0
if grep -Eq '^DKR_EXPERIMENTAL_RACE_TEST:BOOL=(ON|1|TRUE|YES)[[:space:]]*$' "${BUILD_DIRECTORY}/CMakeCache.txt"; then
  EXPERIMENTAL_RACE_TEST=1
  [[ "${VERSION}" == *experimental* || "${VERSION}" == *rollback-test* || "${VERSION}" == 1.0.5-beta.15 || "${VERSION}" == 1.0.5-beta.15-playtest.[23456789] || "${VERSION}" == 1.0.5-beta.15-playtest.10 || "${VERSION}" == 1.0.5-beta.15-playtest.11 || "${VERSION}" == 1.0.5-beta.15-playtest.12 || "${VERSION}" == 1.0.5-beta.15-playtest.14 || "${VERSION}" == 1.0.5-beta.15-playtest.15 || "${VERSION}" == 1.0.5-beta.15-playtest.15-fix.[12] ]] || { echo 'Experimental builds require a distinct experimental version or an approved Beta 15 playtest candidate.' >&2; exit 1; }
fi
# Explicit beta qualification deferral; normal release packaging still runs all checks.
SKIP_RUNTIME_TESTS="${DKR_SKIP_RUNTIME_TESTS:-0}"
[[ "${SKIP_RUNTIME_TESTS}" == 0 || "${SKIP_RUNTIME_TESTS}" == 1 ]] || {
  echo 'DKR_SKIP_RUNTIME_TESTS must be 0 or 1' >&2; exit 1;
}
ARCH="${DKR_LINUX_ARCH:-x86_64}"
case "${ARCH}" in
  x86_64) ELF_MACHINE='Advanced Micro Devices X86-64' ;;
  aarch64) ELF_MACHINE='AArch64' ;;
  *) echo "Unsupported AppImage architecture: ${ARCH}" >&2; exit 1 ;;
esac
APPDIR="${DKR_APPDIR:-${PROJECT_ROOT}/dist/DKR-R-${VERSION}-Linux-${ARCH}.AppDir}"
OUTPUT="${DKR_APPIMAGE_OUTPUT:-${PROJECT_ROOT}/dist/DKR-R-${VERSION}-Linux-${ARCH}.AppImage}"
LINUXDEPLOY="${LINUXDEPLOY:-${PROJECT_ROOT}/.deps/tools/linuxdeploy-${ARCH}.AppImage}"
APPIMAGE_PLUGIN="${LINUXDEPLOY_PLUGIN_APPIMAGE:-${PROJECT_ROOT}/.deps/tools/linuxdeploy-plugin-appimage}"
ICON_FILE="${DKR_LINUX_ICON_FILE:-${PROJECT_ROOT}/assets/ui/Icons/256x256.png}"
ICON_STAGE="$(mktemp -d "${TMPDIR:-/tmp}/dkr-r-icon.XXXXXX")"
BINARY_STAGE="$(mktemp -d "${TMPDIR:-/tmp}/dkr-r-binary.XXXXXX")"
PAK_TEST="$(mktemp -d "${TMPDIR:-/tmp}/dkr-r-appimage-pak.XXXXXX")"
INPUT_SWITCH_TEST="$(mktemp -d "${TMPDIR:-/tmp}/dkr-r-appimage-input-switch.XXXXXX")"
trap 'rm -rf -- "${ICON_STAGE}" "${BINARY_STAGE}" "${PAK_TEST}" "${INPUT_SWITCH_TEST}"' EXIT

validate_release_tree() {
  local root="$1"
  local file extension magic inspected=0
  while IFS= read -r -d '' file; do
    inspected=$((inspected + 1))
    extension="${file##*.}"
    extension="${extension,,}"
    case "${extension}" in
      z64|v64|n64|eep|mpk|sra|fla|o2r|otr|dkr-probe|dkr-bootstrap|dkr-component)
        echo "Release staging contains prohibited game data: ${file}" >&2
        return 1
        ;;
    esac
    magic="$(od -An -tx1 -N4 "${file}" 2>/dev/null | tr -d '[:space:]')"
    case "${magic}" in
      80371240|37804012|40123780)
        echo "Release staging contains an N64 ROM header: ${file}" >&2
        return 1
        ;;
    esac
  done < <(find "${root}" -type f -print0)
  [[ "${inspected}" -gt 0 ]] || {
    echo "Release staging is empty: ${root}" >&2
    return 1
  }
  echo "Release staging scan passed: ${inspected} files inspected"
}

collect_linux_dependency_notices() {
  local root="$1"
  local copyright_file package_directory
  local notice_root="${root}/usr/share/doc/dkr-port/third-party/linux-packages"
  local manifest="${notice_root}/PACKAGE-MANIFEST.txt"
  local library_count copyright_count

  mkdir -p "${notice_root}/common-licenses"
  : > "${manifest}"
  printf '%s\n' \
    'Debian package notice directories deployed with the Linux dependencies:' \
    >> "${manifest}"

  while IFS= read -r -d '' copyright_file; do
    package_directory="$(basename "$(dirname "${copyright_file}")")"
    printf '%s\n' "${package_directory}" >> "${manifest}"
  done < <(find "${root}/usr/share/doc" -mindepth 2 -maxdepth 2 \
    -type f -name copyright -print0 | sort -z)

  while IFS= read -r -d '' copyright_file; do
    install -m 0644 "${copyright_file}" \
      "${notice_root}/common-licenses/$(basename "${copyright_file}")"
  done < <(find /usr/share/common-licenses -maxdepth 1 -type f -print0)

  library_count="$(find "${root}/usr/lib" -maxdepth 1 -type f | wc -l)"
  copyright_count="$(find "${root}/usr/share/doc" -mindepth 2 -maxdepth 2 \
    -type f -name copyright | wc -l)"
  if [[ "${library_count}" -eq 0 || "${copyright_count}" -eq 0 ]]; then
    echo "Linux dependency deployment did not include libraries and copyright records." >&2
    return 1
  fi
  echo "Collected ${copyright_count} dependency copyright records for ${library_count} bundled libraries"
}

[[ -x "${BINARY}" ]] || { echo "Missing Linux release binary: ${BINARY}" >&2; exit 1; }
if grep -aEq 'DKR_WATER_TEST_MAP|\[perf\]\[private-water-preview\]' "${BINARY}"; then
  echo 'Release binary still contains private water qualification; rebuild after disabling it.' >&2
  exit 1
fi
READELF="${READELF:-readelf}"
command -v "${READELF}" >/dev/null || { echo "Missing ELF inspector: ${READELF}" >&2; exit 1; }
for architecture_file in "${BINARY}" "${MOD_WORKER}" "${INPUT_HOST}" "${SDL3_LIBRARY}"; do
  "${READELF}" -h "${architecture_file}" | grep -F "${ELF_MACHINE}" >/dev/null || {
    echo "Wrong architecture (expected ${ARCH}): ${architecture_file}" >&2; exit 1;
  }
done
[[ -x "${MOD_WORKER}" ]] || { echo "Missing legacy importer: ${MOD_WORKER}" >&2; exit 1; }
[[ -x "${INPUT_HOST}" ]] || { echo "Missing private SDL3 input host: ${INPUT_HOST}" >&2; exit 1; }
[[ -f "${SDL3_LIBRARY}" ]] || { echo "Missing private SDL3 runtime: ${SDL3_LIBRARY}" >&2; exit 1; }
[[ -x "${LINUXDEPLOY}" ]] || { echo "Missing linuxdeploy: ${LINUXDEPLOY}" >&2; exit 1; }
[[ -x "${APPIMAGE_PLUGIN}" ]] || { echo "Missing linuxdeploy AppImage plugin: ${APPIMAGE_PLUGIN}" >&2; exit 1; }
[[ -f "${ICON_FILE}" ]] || { echo "Missing Linux application icon: ${ICON_FILE}" >&2; exit 1; }
command -v patchelf >/dev/null || { echo "Missing required command: patchelf" >&2; exit 1; }
[[ ! -e "${APPDIR}" ]] || { echo "AppDir already exists; choose a fresh DKR_APPDIR: ${APPDIR}" >&2; exit 1; }
[[ ! -e "${OUTPUT}" ]] || { echo "Output already exists; choose a fresh DKR_APPIMAGE_OUTPUT: ${OUTPUT}" >&2; exit 1; }

mkdir -p "${APPDIR}/usr/share/doc/dkr-port/licenses" "$(dirname "${OUTPUT}")"
mkdir -p "${APPDIR}/usr/share/metainfo"
install -m 0644 "${ICON_FILE}" "${ICON_STAGE}/dkr-r.png"
install -m 0755 "${BINARY}" "${BINARY_STAGE}/DKR-R"
EXPERIMENTAL_DEPLOY_ARGS=()
if [[ "${EXPERIMENTAL_RACE_TEST}" == 1 ]]; then
  # The earlier generic file describes Playtest 2; current playtests ship their own
  # current connection/scope guide instead of contradictory version guidance.
  if [[ "${VERSION}" != 1.0.5-beta.15-playtest.8 && "${VERSION}" != 1.0.5-beta.15-playtest.9 && "${VERSION}" != 1.0.5-beta.15-playtest.10 && "${VERSION}" != 1.0.5-beta.15-playtest.11 && "${VERSION}" != 1.0.5-beta.15-playtest.12 && "${VERSION}" != 1.0.5-beta.15-playtest.14 && "${VERSION}" != 1.0.5-beta.15-playtest.15 && "${VERSION}" != 1.0.5-beta.15-playtest.15-fix.[12] ]]; then
    install -m 0644 "${PROJECT_ROOT}/docs/EXPERIMENTAL-ROLLBACK-TESTING.txt" "${APPDIR}/usr/share/doc/dkr-port/EXPERIMENTAL-ROLLBACK-TESTING.txt"
  fi
  if [[ "${VERSION}" == 1.0.5-beta.15-playtest.3 ]]; then
    install -m 0644 "${PROJECT_ROOT}/docs/BETA15-PLAYTEST3-PARITY.txt" "${APPDIR}/usr/share/doc/dkr-port/PLAYTEST3-NOTES.txt"
  fi
  if [[ "${VERSION}" == 1.0.5-beta.15-playtest.4 ]]; then
    install -m 0644 "${PROJECT_ROOT}/docs/BETA15-PLAYTEST4-PERFORMANCE.txt" "${APPDIR}/usr/share/doc/dkr-port/PLAYTEST4-NOTES.txt"
  fi
  if [[ "${VERSION}" == 1.0.5-beta.15-playtest.5 ]]; then
    install -m 0644 "${PROJECT_ROOT}/docs/BETA15-PLAYTEST5-BOSS-DIAGNOSTICS.txt" "${APPDIR}/usr/share/doc/dkr-port/PLAYTEST5-NOTES.txt"
  fi
  if [[ "${VERSION}" == 1.0.5-beta.15-playtest.6 ]]; then
    install -m 0644 "${PROJECT_ROOT}/docs/BETA15-PLAYTEST6-PERFORMANCE.txt" "${APPDIR}/usr/share/doc/dkr-port/PLAYTEST6-NOTES.txt"
  fi
  if [[ "${VERSION}" == 1.0.5-beta.15-playtest.7 ]]; then
    install -m 0644 "${PROJECT_ROOT}/docs/BETA15-PLAYTEST7-CPU.txt" "${APPDIR}/usr/share/doc/dkr-port/PLAYTEST7-NOTES.txt"
    install -m 0644 "${PROJECT_ROOT}/docs/EXPERIMENTAL-LOCAL-SCENERY-STATUS-20261003.txt" "${APPDIR}/usr/share/doc/dkr-port/LOCAL-SCENERY-STATUS.txt"
  fi
  if [[ "${VERSION}" == 1.0.5-beta.15-playtest.8 ]]; then
    install -m 0644 "${PROJECT_ROOT}/docs/BETA15-PLAYTEST8-SCENERY.txt" "${APPDIR}/usr/share/doc/dkr-port/PLAYTEST8-NOTES.txt"
    install -m 0644 "${PROJECT_ROOT}/docs/EXPERIMENTAL-LOCAL-SCENERY-STATUS-20261003.txt" "${APPDIR}/usr/share/doc/dkr-port/LOCAL-SCENERY-STATUS.txt"
  fi
  if [[ "${VERSION}" == 1.0.5-beta.15-playtest.9 ]]; then
    install -m 0644 "${PROJECT_ROOT}/docs/BETA15-PLAYTEST9-CAMERA.txt" "${APPDIR}/usr/share/doc/dkr-port/PLAYTEST9-NOTES.txt"
    install -m 0644 "${PROJECT_ROOT}/docs/EXPERIMENTAL-LOCAL-SCENERY-STATUS-20261003.txt" "${APPDIR}/usr/share/doc/dkr-port/LOCAL-SCENERY-STATUS.txt"
  fi
  if [[ "${VERSION}" == 1.0.5-beta.15-playtest.10 || "${VERSION}" == 1.0.5-beta.15-playtest.11 || "${VERSION}" == 1.0.5-beta.15-playtest.12 || "${VERSION}" == 1.0.5-beta.15-playtest.14 ]]; then
    install -m 0644 "${PROJECT_ROOT}/docs/BETA15-PLAYTEST10-SAVE-PROTECTION.txt" "${APPDIR}/usr/share/doc/dkr-port/PLAYTEST10-NOTES.txt"
    install -m 0644 "${PROJECT_ROOT}/docs/CONTROLLER-STUTTER-AND-LAUNCHER-OPTIONS-20261004.txt" "${APPDIR}/usr/share/doc/dkr-port/LAUNCHER-OPTIONS-AND-CONTROLLERS.txt"
    install -m 0644 "${PROJECT_ROOT}/docs/EXPERIMENTAL-LOCAL-SCENERY-STATUS-20261003.txt" "${APPDIR}/usr/share/doc/dkr-port/LOCAL-SCENERY-STATUS.txt"
  fi
  if [[ "${VERSION}" == 1.0.5-beta.15-playtest.11 ]]; then
    install -m 0644 "${PROJECT_ROOT}/docs/BETA15-PLAYTEST11-CUSTOM-ROSTER.txt" "${APPDIR}/usr/share/doc/dkr-port/PLAYTEST11-NOTES.txt"
  fi
  if [[ "${VERSION}" == 1.0.5-beta.15-playtest.12 ]]; then
    install -m 0644 "${PROJECT_ROOT}/docs/BETA15-PLAYTEST12-ONLINE-MODS.txt" "${APPDIR}/usr/share/doc/dkr-port/PLAYTEST12-NOTES.txt"
  fi
  if [[ "${VERSION}" == 1.0.5-beta.15-playtest.14 || "${VERSION}" == 1.0.5-beta.15-playtest.15 || "${VERSION}" == 1.0.5-beta.15-playtest.15-fix.[12] ]]; then
    install -m 0644 "${PROJECT_ROOT}/docs/BETA15-PLAYTEST14-ONLINE-REVISIONS.txt" "${APPDIR}/usr/share/doc/dkr-port/PLAYTEST14-NOTES.txt"
    install -m 0644 "${PROJECT_ROOT}/docs/PLAYTEST14-ONLINE-RETENTION-AND-PROGRESS.txt" "${APPDIR}/usr/share/doc/dkr-port/ONLINE-MOD-ADMISSION-REBUILD.txt"
    install -m 0644 "${PROJECT_ROOT}/docs/PLAYTEST14-MOD-SYNC-PERFORMANCE-20261006.txt" "${APPDIR}/usr/share/doc/dkr-port/ONLINE-MOD-SYNC-PERFORMANCE.txt"
  fi
  if [[ "${VERSION}" == 1.0.5-beta.15-playtest.15 || "${VERSION}" == 1.0.5-beta.15-playtest.15-fix.[12] ]]; then
    install -m 0644 "${PROJECT_ROOT}/docs/BETA15-PLAYTEST15-ROLLBACK.txt" "${APPDIR}/usr/share/doc/dkr-port/PLAYTEST15-NOTES.txt"
  fi
  if [[ "${VERSION}" == 1.0.5-beta.15-playtest.15-fix.[12] ]]; then
    install -m 0644 "${PROJECT_ROOT}/docs/BETA15-PLAYTEST15-FIX1.txt" "${APPDIR}/usr/share/doc/dkr-port/PLAYTEST15-FIX1-NOTES.txt"
  fi
  if [[ "${VERSION}" == 1.0.5-beta.15-playtest.15-fix.2 ]]; then
    install -m 0644 "${PROJECT_ROOT}/docs/BETA15-PLAYTEST15-FIX2.txt" "${APPDIR}/usr/share/doc/dkr-port/PLAYTEST15-FIX2-NOTES.txt"
  fi
fi
install -m 0644 "${PROJECT_ROOT}/LICENSE.md" "${APPDIR}/usr/share/doc/dkr-port/LICENSE.md"
install -m 0644 "${PROJECT_ROOT}/THIRD_PARTY.md" "${APPDIR}/usr/share/doc/dkr-port/THIRD_PARTY.md"
install -m 0644 "${PROJECT_ROOT}/docs/ONLINE_MULTIPLAYER.md" "${APPDIR}/usr/share/doc/dkr-port/ONLINE_MULTIPLAYER.md"
install -m 0644 "${PROJECT_ROOT}/docs/ONLINE-STABILITY.md" "${APPDIR}/usr/share/doc/dkr-port/ONLINE-STABILITY.md"
install -m 0644 "${PROJECT_ROOT}/docs/HUD-WORKSHOP.md" "${APPDIR}/usr/share/doc/dkr-port/HUD-WORKSHOP.md"
install -m 0644 "${PROJECT_ROOT}/runtime-recomp/COPYING-NOTICE.md" "${APPDIR}/usr/share/doc/dkr-port/COPYING-NOTICE.md"
install -m 0644 "${PROJECT_ROOT}/extern/rt64/LICENSE" "${APPDIR}/usr/share/doc/dkr-port/licenses/RT64-LICENSE.txt"
install -m 0644 "${PROJECT_ROOT}/extern/rt64/src/contrib/imgui/LICENSE.txt" "${APPDIR}/usr/share/doc/dkr-port/licenses/Dear-ImGui-LICENSE.txt"
install -m 0644 "${PROJECT_ROOT}/extern/rt64/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/COPYING.txt" "${APPDIR}/usr/share/doc/dkr-port/licenses/SDL2-LICENSE.txt"
install -m 0644 "${PROJECT_ROOT}/extern/sdl3/LICENSE.txt" "${APPDIR}/usr/share/doc/dkr-port/licenses/SDL3-LICENSE.txt"
install -m 0644 "${PROJECT_ROOT}/extern/n64-modern-runtime/COPYING" "${APPDIR}/usr/share/doc/dkr-port/licenses/N64ModernRuntime-COPYING.txt"
install -m 0644 "${PROJECT_ROOT}/extern/n64-modern-runtime/N64Recomp/LICENSE" "${APPDIR}/usr/share/doc/dkr-port/licenses/N64Recomp-LICENSE.txt"
install -m 0644 "${PROJECT_ROOT}/packaging/licenses/Jumpman-LICENSE.txt" "${APPDIR}/usr/share/doc/dkr-port/licenses/Jumpman-LICENSE.txt"
install -m 0644 "${PROJECT_ROOT}/packaging/licenses/Selawik-OFL.txt" "${APPDIR}/usr/share/doc/dkr-port/licenses/Selawik-OFL.txt"
install -m 0644 "${PROJECT_ROOT}/packaging/licenses/CRT-FILTERS-NOTICE.md" "${APPDIR}/usr/share/doc/dkr-port/licenses/CRT-FILTERS-NOTICE.md"
install -m 0644 "${PROJECT_ROOT}/packaging/licenses/SDL-GAMECONTROLLERDB-LICENSE.txt" "${APPDIR}/usr/share/doc/dkr-port/licenses/SDL-GAMECONTROLLERDB-LICENSE.txt"
install -m 0644 "${PROJECT_ROOT}/packaging/licenses/GEKKONET-LICENSE.txt" "${APPDIR}/usr/share/doc/dkr-port/licenses/GEKKONET-LICENSE.txt"
install -m 0644 "${PROJECT_ROOT}/packaging/licenses/MONOCYPHER-LICENSE.txt" "${APPDIR}/usr/share/doc/dkr-port/licenses/MONOCYPHER-LICENSE.txt"
install -m 0644 "${PROJECT_ROOT}/packaging/licenses/GOLDEN-BALLOON-NOTICE.txt" "${APPDIR}/usr/share/doc/dkr-port/licenses/GOLDEN-BALLOON-NOTICE.txt"
install -m 0644 "${PROJECT_ROOT}/packaging/licenses/LEGACY-MODS-NOTICE.txt" "${APPDIR}/usr/share/doc/dkr-port/licenses/LEGACY-MODS-NOTICE.txt"
install -m 0644 "${PROJECT_ROOT}/docs/LEGACY-MODS-BETA.md" "${APPDIR}/usr/share/doc/dkr-port/LEGACY-MODS-BETA.md"
install -m 0644 "${PROJECT_ROOT}/docs/LEGACY-MOD-COMPATIBILITY-BETA7.md" "${APPDIR}/usr/share/doc/dkr-port/LEGACY-MOD-COMPATIBILITY-BETA7.md"
install -m 0644 "${PROJECT_ROOT}/extern/libdatachannel/LICENSE" "${APPDIR}/usr/share/doc/dkr-port/licenses/LIBDATACHANNEL-LICENSE.txt"
install -m 0644 "${PROJECT_ROOT}/extern/mbedtls/LICENSE" "${APPDIR}/usr/share/doc/dkr-port/licenses/MBEDTLS-LICENSE.txt"
install -m 0644 "${PROJECT_ROOT}/extern/libdatachannel/deps/libjuice/LICENSE" "${APPDIR}/usr/share/doc/dkr-port/licenses/LIBJUICE-LICENSE.txt"
install -m 0644 "${PROJECT_ROOT}/extern/libdatachannel/deps/usrsctp/LICENSE.md" "${APPDIR}/usr/share/doc/dkr-port/licenses/USRSCTP-LICENSE.txt"
install -m 0644 "${PROJECT_ROOT}/extern/libdatachannel/deps/json/LICENSE.MIT" "${APPDIR}/usr/share/doc/dkr-port/licenses/NLOHMANN-JSON-LICENSE.txt"
install -m 0644 "${PROJECT_ROOT}/extern/libdatachannel/deps/plog/LICENSE" "${APPDIR}/usr/share/doc/dkr-port/licenses/PLOG-LICENSE.txt"
install -m 0644 "${PROJECT_ROOT}/packaging/linux/dkr-port.appdata.xml" "${APPDIR}/usr/share/metainfo/dkr-port.appdata.xml"

export PATH="$(dirname "${APPIMAGE_PLUGIN}"):${PATH}"
export OUTPUT
export VERSION
export LDAI_OUTPUT="${OUTPUT}"
export LINUXDEPLOY_OUTPUT_VERSION="${VERSION}"
export APPIMAGE_EXTRACT_AND_RUN=1
# This local release has no public project homepage yet. appimagetool treats
# that optional AppStream field as a fatal warning, so package the supplied
# metadata without the network-facing catalogue validation step.
export LDAI_NO_APPSTREAM=1

run_appimage() {
  if [[ -n "${DKR_APPIMAGE_RUNNER:-}" ]]; then
    bash "${DKR_APPIMAGE_RUNNER}" "$@"
  else
    "$@"
  fi
}

run_appimage "${LINUXDEPLOY}" --appimage-extract-and-run \
  --appdir "${APPDIR}" \
  --executable "${BINARY_STAGE}/DKR-R" \
  "${EXPERIMENTAL_DEPLOY_ARGS[@]}" \
  --desktop-file "${PROJECT_ROOT}/packaging/linux/dkr-port.desktop" \
  --icon-file "${ICON_STAGE}/dkr-r.png"

mkdir -p "${APPDIR}/usr/bin/assets/ui/Icons"
install -m 0644 "${PROJECT_ROOT}/assets/ui/Icons/DKR-R-Logo.bmp" \
  "${APPDIR}/usr/bin/assets/ui/Icons/DKR-R-Logo.bmp"
install -m 0644 "${PROJECT_ROOT}/assets/ui/Icons/DKR-R-Spinning-Icon.png" \
  "${APPDIR}/usr/bin/assets/ui/Icons/DKR-R-Spinning-Icon.png"
install -m 0644 "${PROJECT_ROOT}/assets/ui/Icons/DKR-R-Short-Logo.png" \
  "${APPDIR}/usr/bin/assets/ui/Icons/DKR-R-Short-Logo.png"
mkdir -p "${APPDIR}/usr/bin/assets/ui/Backgrounds"
install -m 0644 \
  "${PROJECT_ROOT}/assets/ui/Backgrounds/DKR-R-Launcher-Background.png" \
  "${APPDIR}/usr/bin/assets/ui/Backgrounds/DKR-R-Launcher-Background.png"
mkdir -p "${APPDIR}/usr/bin/assets/filters"
install -m 0644 "${PROJECT_ROOT}"/assets/filters/*.png \
  "${APPDIR}/usr/bin/assets/filters/"
mkdir -p "${APPDIR}/usr/bin/assets/controllers"
install -m 0644 "${PROJECT_ROOT}/assets/controllers/gamecontrollerdb.txt" \
  "${APPDIR}/usr/bin/assets/controllers/gamecontrollerdb.txt"

# Keep SDL3 outside usr/bin so it cannot replace or interpose on the SDL2 ABI
# used by the single launcher/game window. The helper has an $ORIGIN rpath and
# therefore resolves only the private copy installed beside it.
mkdir -p "${APPDIR}/usr/libexec/dkr-r"
install -m 0755 "${MOD_WORKER}" "${APPDIR}/usr/libexec/dkr-r/DKR-R-ModWorker"
patchelf --set-rpath '$ORIGIN/../../lib' "${APPDIR}/usr/libexec/dkr-r/DKR-R-ModWorker"
install -m 0755 "${INPUT_HOST}" \
  "${APPDIR}/usr/libexec/dkr-r/DKR-R-InputHost"
install -m 0755 "${SDL3_LIBRARY}" \
  "${APPDIR}/usr/libexec/dkr-r/libSDL3.so.0"
patchelf --set-rpath '$ORIGIN' \
  "${APPDIR}/usr/libexec/dkr-r/DKR-R-InputHost"
if [[ "${SKIP_RUNTIME_TESTS}" == 0 ]]; then
timeout 15 "${APPDIR}/usr/libexec/dkr-r/DKR-R-ModWorker" --self-test
[[ "$(timeout 15 "${APPDIR}/usr/libexec/dkr-r/DKR-R-ModWorker" --online-capability)" == 'DKR-R online-prepare protocol 1' ]] || { echo 'Packaged ModWorker lacks online preparation support.' >&2; exit 1; }
LD_LIBRARY_PATH="${APPDIR}/usr/libexec/dkr-r${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
  "${APPDIR}/usr/libexec/dkr-r/DKR-R-InputHost" --self-test --mappings \
  "${APPDIR}/usr/bin/assets/controllers/gamecontrollerdb.txt"
fi

collect_linux_dependency_notices "${APPDIR}"
validate_release_tree "${APPDIR}"
run_appimage "${APPIMAGE_PLUGIN}" --appdir "${APPDIR}"
[[ -s "${OUTPUT}" ]] || { echo "AppImage output is missing or empty: ${OUTPUT}" >&2; exit 1; }
if [[ "${SKIP_RUNTIME_TESTS}" == 0 ]]; then
APPIMAGE_EXTRACT_AND_RUN=1 run_appimage "${OUTPUT}" --self-test-pak "${PAK_TEST}"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy APPIMAGE_EXTRACT_AND_RUN=1 \
  run_appimage "${OUTPUT}" --self-test-input-switch "${INPUT_SWITCH_TEST}"
else
  echo 'WARNING: Runtime tests explicitly deferred; package runtime qualification remains pending.' >&2
fi
echo "Created ${OUTPUT}"
sha256sum "${OUTPUT}"
