#!/usr/bin/env bash
# Prepare everything Build-macOS.sh compiles: pinned dependencies with their
# patches, N64Recomp, and the recompiled CPU payloads for both supported ROM
# revisions (US v1.0 = v77, US Rev A/v1.1 = v80). Re-running is cheap: the
# decomp ELF and payload stages are skipped while their inputs are unchanged.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
source "${PROJECT_ROOT}/scripts/lib/apple-build.sh"
# Inherit the same lock when Build invokes preparation; release on process exit.
if [[ "${DKR_MACOS_LOCK_ENTRY:-}" != "${BASH_SOURCE[0]}" ]]; then
  exec python3 "${PROJECT_ROOT}/scripts/macos_build_lock.py" "${PROJECT_ROOT}" "${BASH_SOURCE[0]}" "$@"
fi
unset DKR_MACOS_LOCK_ENTRY
LOG_DIR="$PROJECT_ROOT/build-logs"
LOG_PATH="$LOG_DIR/prepare-dkr-runtime-$(date '+%Y%m%d-%H%M%S').log"

ROM_V77="${DKR_ROM_V77:-}"
ROM_V80="${DKR_ROM_V80:-}"
OUTPUT="${DKR_GENERATED_ROOT:-$PROJECT_ROOT/build/generated}"
SKIP_DEPS=0
FORCE=0
JOBS="${CMAKE_BUILD_PARALLEL_LEVEL:-}"

usage() {
  cat <<EOF
Usage: $(basename "$0") --rom-v77 <US 1.0 ROM> --rom-v80 <US Rev A ROM> [options]
  --rom-v77 PATH   Diddy Kong Racing US v1.0 (.z64/.v64/.n64), or \$DKR_ROM_V77
  --rom-v80 PATH   Diddy Kong Racing US Rev A/v1.1, or \$DKR_ROM_V80
  --output DIR     Generated payload root (default: build/generated)
  --skip-deps      Don't install Homebrew packages
  --force          Rebuild the decomp ELFs and payloads even if their inputs are unchanged
  --jobs N         Parallel jobs (default: host CPU count)
Writes <output>/v77/RecompiledFuncs and <output>/v80/RecompiledFuncs, then
the build inputs scripts/prepare_payloads.py derives from them to
build/prepared.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
  --rom-v77)
    [[ $# -ge 2 && "$2" != --* && -n "$2" ]] || {
      echo "$1 requires a path." >&2
      exit 1
    }
    ROM_V77="$2"
    shift
    ;;
  --rom-v80)
    [[ $# -ge 2 && "$2" != --* && -n "$2" ]] || {
      echo "$1 requires a path." >&2
      exit 1
    }
    ROM_V80="$2"
    shift
    ;;
  --output)
    [[ $# -ge 2 && "$2" != --* && -n "$2" ]] || {
      echo "$1 requires a path." >&2
      exit 1
    }
    OUTPUT="$2"
    shift
    ;;
  --skip-deps) SKIP_DEPS=1 ;;
  --force) FORCE=1 ;;
  --jobs)
    [[ $# -ge 2 && "$2" != --* && -n "$2" ]] || {
      echo "$1 requires a value." >&2
      exit 1
    }
    JOBS="$2"
    shift
    ;;
  -h | --help)
    usage
    exit 0
    ;;
  *)
    echo "Unknown option: $1" >&2
    usage >&2
    exit 1
    ;;
  esac
  shift
done

dkr_require_macos
JOBS="$(dkr_apple_build_jobs "$JOBS")"
mkdir -p "$LOG_DIR"
exec > >(tee -a "$LOG_PATH") 2>&1

fail() {
  echo "[ERROR] $*" >&2
  echo "Full log: $LOG_PATH" >&2
  exit 1
}
step() {
  echo
  echo "==> $*"
}
# Keep paths valid when N64Recomp changes its working directory.
[[ "$OUTPUT" == /* ]] || OUTPUT="$PWD/$OUTPUT"
[[ -n "$ROM_V77" && -f "$ROM_V77" ]] || {
  usage >&2
  fail "--rom-v77 must name your US v1.0 ROM."
}
[[ -n "$ROM_V80" && -f "$ROM_V80" ]] || {
  usage >&2
  fail "--rom-v80 must name your US Rev A/v1.1 ROM."
}

step "Host tools"
source "$SCRIPT_DIR/macos-host-tools.sh"
dkr_macos_host_tools "$SKIP_DEPS" || fail "Host prerequisite check failed."
# Never retire generated output through an unresolved/aliased source path.
OUTPUT="$(
  python3 - "$OUTPUT" "$PROJECT_ROOT" <<'PY'
import pathlib, sys
target, root = (pathlib.Path(p).resolve() for p in sys.argv[1:])
blocked = [pathlib.Path.home().resolve(), root, root / "extern",
           root / "runtime-recomp" / "RecompiledFuncs",
           root / "runtime-recomp" / "RecompiledPatches"]
stages = [target] + [(root / "build" / "decomp" / ("v" + str(r))).resolve()
                     for r in (77, 80)]
for stage in stages:
    if stage == pathlib.Path(stage.anchor) or any(
            stage == p or stage in p.parents for p in blocked) or any(
            p == stage or p in stage.parents for p in blocked[2:]):
        raise SystemExit("output and decomp stages must be dedicated build directories")
for revision in (77, 80):
    child = target / ("v" + str(revision))
    if child.is_symlink():
        raise SystemExit("refusing symlinked generated revision: " + str(child))
    tree = root / "build" / "decomp" / ("v" + str(revision))
    if tree.is_symlink():
        raise SystemExit("refusing symlinked decomp stage: " + str(tree))
print(target)
PY
)" || fail "Unsafe generated output directory."

step "Pinned dependencies"
# Read the lock first: a failure inside a process substitution would be lost.
pinned="$(
  python3 - "$PROJECT_ROOT/dependencies.lock.json" <<'PY'
import json, sys
for d in json.load(open(sys.argv[1]))["dependencies"]:
    print(d["name"], d["destination"], d["commit"], str(d.get("recursive", False)).lower(), sep="\t")
PY
)" || fail "Could not read dependencies.lock.json"
# Only fetch what isn't already at its pinned commit: patched checkouts are
# intentionally dirty and must not be reset on every run.
while IFS=$'\t' read -r name destination commit recursive; do
  if [[ -e "$PROJECT_ROOT/$destination/.git" ]] &&
    [[ "$(git -C "$PROJECT_ROOT/$destination" rev-parse HEAD 2>/dev/null)" == "$commit" ]]; then
    echo "[OK] $name at ${commit:0:12}"
  elif [[ "$name" == "dkr-decomp" ]]; then
    "$SCRIPT_DIR/Prepare-Game-Source-macOS.sh"
  else
    python3 "$SCRIPT_DIR/bootstrap_dependencies.py" --only "$name"
  fi
  # Recover interrupted submodule preparation even when HEAD is already pinned.
  if [[ "$recursive" == true ]]; then
    git -C "$PROJECT_ROOT/$destination" submodule update --init --recursive
  fi
done <<<"$pinned"
git -C "$PROJECT_ROOT/extern/dkr-decomp" submodule update --init --recursive

RT64="$PROJECT_ROOT/extern/rt64"
# The RT64 patches and runtime CMake expect metal-cpp at src/contrib/metal-cpp.
if [[ -d "$RT64/src/contrib/plume/contrib/metal-cpp" && ! -e "$RT64/src/contrib/metal-cpp" ]]; then
  ln -s plume/contrib/metal-cpp "$RT64/src/contrib/metal-cpp"
fi

step "Dependency patches"
python3 "$SCRIPT_DIR/apply_dependency_patches.py"

step "N64Recomp"
TOOL_BUILD="$PROJECT_ROOT/build/runtime-tools/n64recomp"
cmake -S "$PROJECT_ROOT/extern/n64-modern-runtime/N64Recomp" -B "$TOOL_BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$TOOL_BUILD" --parallel "$JOBS" --target N64RecompCLI
RECOMPILER="$TOOL_BUILD/N64Recomp"
[[ -x "$RECOMPILER" ]] || fail "N64Recomp was not built: $RECOMPILER"

step "Decomp toolchain"
DECOMP="$PROJECT_ROOT/extern/dkr-decomp"
BINUTILS_CACHE="$PROJECT_ROOT/build/runtime-tools/mips-binutils"
BINUTILS_SOURCE="$DECOMP/tools/binutils"
# The Makefile uses a PATH toolchain only when both ld and gcc exist, else
# tools/binutils. ld is copied last, so its presence means a complete set.
if ! [[ -x "$DECOMP/tools/binutils/mips64-elf-ld" ]] &&
  ! { command -v mips64-elf-ld >/dev/null && command -v mips64-elf-gcc >/dev/null; }; then
  BINUTILS_SOURCE="$BINUTILS_CACHE"
  if ! [[ -x "$BINUTILS_CACHE/mips64-elf-ld" ]]; then
    # The decomp's get-binutils.sh uses Linux-only nproc; build them here.
    version="binutils-2.46.0"
    scratch="$(mktemp -d "${TMPDIR:-/tmp}/dkr-binutils.XXXXXX")"
    trap 'rm -rf "$scratch"' EXIT
    tarball="$scratch/$version.tar.xz"
    curl -fsSL -o "$tarball" "https://ftp.gnu.org/gnu/binutils/$version.tar.xz" || fail "Downloading $version failed."
    echo "d75a94f4d73e7a4086f7513e67e439e8fcdcbb726ffe63f4661744e6256b2cf2  $tarball" | shasum -a 256 -c - >/dev/null ||
      fail "$version checksum mismatch."
    tar -xJf "$tarball" -C "$scratch"
    (
      cd "$scratch/$version"
      ./configure --target=mips64-elf --prefix=/usr --with-sysroot=/usr/mips64-elf \
        --enable-64-bit-bfd --disable-gold --disable-nls --disable-shared --disable-werror >/dev/null
      make -j"$JOBS" >/dev/null
    ) || fail "Building MIPS binutils failed."
    mkdir -p "$BINUTILS_CACHE"
    for pair in binutils/ar:ar binutils/objcopy:objcopy binutils/objdump:objdump \
      binutils/strip-new:strip gas/as-new:as ld/ld-new:ld; do
      cp "$scratch/$version/${pair%%:*}" "$BINUTILS_CACHE/mips64-elf-${pair##*:}"
    done
  fi
fi

# Setup must recover missing native tools as well as the Python environment.
# Check and prepare the isolated revision tree, never the live dependency.
decomp_ready() {
  local tree="$1"
  [[ -x "$tree/.venv/bin/python3" ]] && "$tree/.venv/bin/python3" -c 'import splat' 2>/dev/null &&
    [[ -x "$tree/tools/dkr_assets_tool" && -x "$tree/tools/n64crc" && -x "$tree/tools/ido-recomp/macos/cc" ]]
}

# stamp_matches <file> <value>: true when the stage's recorded inputs match.
stamp_matches() { [[ "$FORCE" -eq 0 && -f "$1" && "$(cat "$1")" == "$2" ]]; }

prepare_revision() {
  local revision="$1" rom="$2" sha1="$3"
  local tree="$PROJECT_ROOT/build/decomp/v$revision"
  local elf="$tree/build/dkr.us.v$revision.elf" built_rom="$tree/build/dkr.us.v$revision.z64"
  local work="$OUTPUT/v$revision"

  step "US v$revision: ROM"
  # Normalise byte order and verify the exact retail dump.
  python3 "$SCRIPT_DIR/compose_payloads.py" rom --revision "$revision" "$rom" "$tree/baseroms/baserom.us.v$revision.z64" ||
    fail "The US v$revision ROM was rejected."

  step "US v$revision: decomp ELF"
  # Each revision gets its own tree: the decomp extracts every version into
  # the same asm/, assets/ and build/ directories.
  local decomp_commit
  decomp_commit="$(git -C "$DECOMP" rev-parse HEAD)"
  if stamp_matches "$tree/.dkr-elf-stamp" "$decomp_commit $sha1" && [[ -f "$elf" && -f "$built_rom" ]]; then
    echo "[OK] up to date: $elf"
  else
    rsync -a --delete --exclude /.git --exclude /build --exclude /asm --exclude /assets \
      --exclude /baseroms --exclude /.venv --exclude /.dkr-elf-stamp "$DECOMP/" "$tree/"
    if [[ -x "$BINUTILS_SOURCE/mips64-elf-ld" ]]; then
      mkdir -p "$tree/tools/binutils"
      cp "$BINUTILS_SOURCE"/mips64-elf-* "$tree/tools/binutils/"
      touch "$tree/tools/binutils/.dkr-toolchain"
    fi
    if ! decomp_ready "$tree"; then
      (cd "$tree" && "$GMAKE" setup) || fail "Decomp setup failed."
      decomp_ready "$tree" || fail "Decomp setup did not produce its tools; see the log above."
    fi
    (cd "$tree" && "$GMAKE" VERSION="v$revision" extract && "$GMAKE" VERSION="v$revision" -j"$JOBS") ||
      fail "The US v$revision decomp build failed."
    echo "$decomp_commit $sha1" >"$tree/.dkr-elf-stamp"
  fi

  step "US v$revision: recompiled payload"
  local inputs
  inputs="$(cat "$PROJECT_ROOT"/runtime-recomp/*.v"$revision".*.json "$PROJECT_ROOT"/scripts/*.py \
    "$SCRIPT_DIR/Prepare-DKR-Runtime-macOS.sh" "$elf" "$RECOMPILER" | shasum -a 256 | cut -d' ' -f1)"
  if stamp_matches "$work/.dkr-payload-stamp" "$inputs" && [[ -f "$work/RecompiledFuncs/funcs.h" ]]; then
    echo "[OK] up to date: $work/RecompiledFuncs"
    return
  fi
  if [[ -e "$work" ]]; then
    [[ -f "$work/.dkr-payload-stamp" ]] || fail "Refusing to replace unmanaged output: $work"
    local previous="$work.previous-$(date '+%Y%m%d-%H%M%S')-$$"
    [[ ! -e "$previous" ]] || fail "Recovery output already exists: $previous"
    mv "$work" "$previous"
    echo "[OK] Previous generated output preserved: $previous"
  fi
  python3 "$SCRIPT_DIR/compose_payloads.py" payload --revision "$revision" --elf "$elf" --rom "$built_rom" \
    --recompiler "$RECOMPILER" --output "$work" || fail "The US v$revision payload failed (log: $work/n64recomp.log)."
  echo "$inputs" >"$work/.dkr-payload-stamp"
  echo "[OK] $work/RecompiledFuncs"
}

# The retail SHA-1s (compose_payloads.py checks the ROMs against the same).
prepare_revision 77 "$ROM_V77" 0cb115d8716dbbc2922fda38e533b9fe63bb9670
prepare_revision 80 "$ROM_V80" 6d96743d46f8c0cd0edb0ec5600b003c89b93755

# The build inputs derived from both payloads (save layout and portrait
# stages, owned engine, Camera++ and model kernels); CMake reads them by default.
step "Prepared build inputs"
prepared_inputs=()
for revision in 77 80; do
  prepared_inputs+=(--input "$revision" "$OUTPUT/v$revision/RecompiledFuncs" "$OUTPUT/v$revision/dkr.us.v$revision.toml"
    "$PROJECT_ROOT/build/decomp/v$revision/build/dkr.us.v$revision.elf")
done
python3 -B "$SCRIPT_DIR/prepare_payloads.py" "${prepared_inputs[@]}" || fail "Preparing the build inputs failed."

echo
echo "[OK] Payloads ready in $OUTPUT, build inputs in $PROJECT_ROOT/build/prepared"
echo "Full log: $LOG_PATH"
