#!/usr/bin/env bash
# Fail when an ELF inside a Linux AppDir needs a shared library that is neither
# bundled in the AppDir nor on the host-provided allowlist below.
#
# Only DT_NEEDED entries and AppDir file names are inspected, so the result
# does not depend on what the packaging host has installed. A runtime launch
# on the packaging host cannot catch a half-bundled closure because the host
# quietly supplies the missing library (issue #45: libwayland-client).
#
# Usage: Check-AppDir-Closure.sh <AppDir> [--host-resolved <relative path>]...
#   --host-resolved  skip an executable that deliberately resolves everything
#                    from the host (it has no AppDir rpath)
# Environment:
#   READELF                       readelf for the target architecture
#   DKR_APPDIR_EXTRA_HOST_LIBS    extra space-separated SONAMEs to allow
set -euo pipefail

READELF="${READELF:-readelf}"

# Libraries the AppImage expects the host to provide: glibc, the C++ and GCC
# runtimes, and the graphics/audio/font stack that has to match host drivers.
# This mirrors linuxdeploy's excludelist on purpose, except for
# libwayland-client.so.0, which Package-Linux-AppImage.sh ships as a fallback.
HOST_PROVIDED='
ld-linux.so.2
ld-linux-x86-64.so.2
ld-linux-aarch64.so.1
libanl.so.1
libBrokenLocale.so.1
libcidn.so.1
libc.so.6
libdl.so.2
libm.so.6
libmvec.so.1
libnss_compat.so.2
libnss_dns.so.2
libnss_files.so.2
libnss_hesiod.so.2
libnss_nisplus.so.2
libnss_nis.so.2
libpthread.so.0
libresolv.so.2
librt.so.1
libthread_db.so.1
libutil.so.1
libstdc++.so.6
libgcc_s.so.1
libGL.so.1
libEGL.so.1
libGLdispatch.so.0
libGLX.so.0
libOpenGL.so.0
libglapi.so.0
libvulkan.so.1
libdrm.so.2
libgbm.so.1
libxcb.so.1
libxcb-dri2.so.0
libxcb-dri3.so.0
libX11.so.6
libX11-xcb.so.1
libasound.so.2
libjack.so.0
libpipewire-0.3.so.0
libfontconfig.so.1
libfreetype.so.6
libharfbuzz.so.0
libfribidi.so.0
libcom_err.so.2
libexpat.so.1
libgpg-error.so.0
libICE.so.6
libSM.so.6
libusb-1.0.so.0
libuuid.so.1
libz.so.1
libgmp.so.10
'

usage() {
  echo "Usage: $0 <AppDir> [--host-resolved <relative path>]..." >&2
}

[[ $# -ge 1 ]] || {
  usage
  exit 2
}
appdir="${1%/}"
shift
host_resolved=''
while [[ $# -gt 0 ]]; do
  case "$1" in
  --host-resolved)
    [[ $# -ge 2 ]] || {
      usage
      exit 2
    }
    host_resolved="${host_resolved}$2"$'\n'
    shift 2
    ;;
  *)
    usage
    exit 2
    ;;
  esac
done
[[ -d "${appdir}/usr" ]] || {
  echo "Not an AppDir (missing usr/): ${appdir}" >&2
  exit 1
}
command -v "${READELF}" >/dev/null || {
  echo "Missing ELF inspector: ${READELF}" >&2
  exit 1
}

allowed="${HOST_PROVIDED}"
for library in ${DKR_APPDIR_EXTRA_HOST_LIBS:-}; do
  allowed="${allowed}${library}"$'\n'
done

roots=()
for directory in usr/bin usr/lib usr/lib64 usr/libexec; do
  [[ -d "${appdir}/${directory}" ]] && roots+=("${appdir}/${directory}")
done
[[ ${#roots[@]} -gt 0 ]] || {
  echo "AppDir has no binary or library directories: ${appdir}" >&2
  exit 1
}

# Any shared object anywhere under the library and libexec trees counts as
# bundled; rpath correctness is the packaging script's job.
bundled=''
while IFS= read -r -d '' file; do
  bundled="${bundled}${file##*/}"$'\n'
done < <(find "${roots[@]}" \( -type f -o -type l \) -name '*.so*' -print0)

inspected=0
unresolved=0
while IFS= read -r -d '' file; do
  relative="${file#"${appdir}/"}"
  if grep -Fxq -- "${relative}" <<<"${host_resolved}"; then
    continue
  fi
  [[ "$(od -An -tx1 -N4 "${file}" 2>/dev/null | tr -d '[:space:]')" == 7f454c46 ]] || continue
  inspected=$((inspected + 1))
  dynamic="$("${READELF}" -d "${file}")"
  while IFS= read -r library; do
    [[ -n "${library}" ]] || continue
    if grep -Fxq -- "${library}" <<<"${bundled}" || grep -Fxq -- "${library}" <<<"${allowed}"; then
      continue
    fi
    echo "Unbundled dependency: ${relative} needs ${library}" >&2
    unresolved=$((unresolved + 1))
  done < <(sed -n 's/.*(NEEDED).*\[\(.*\)\].*/\1/p' <<<"${dynamic}")
done < <(find "${roots[@]}" -type f -print0 | sort -z)

[[ "${inspected}" -gt 0 ]] || {
  echo "AppDir closure check found no ELF files: ${appdir}" >&2
  exit 1
}
if [[ "${unresolved}" -gt 0 ]]; then
  echo "AppDir closure check failed: ${unresolved} unbundled dependencies across ${inspected} ELF files." >&2
  echo 'Bundle the library, or add it to HOST_PROVIDED in scripts/Check-AppDir-Closure.sh if the host must supply it.' >&2
  exit 1
fi
echo "AppDir closure check passed: ${inspected} ELF files inspected"
