#!/usr/bin/env bash
# Fold equivalence gate, levels 1 to 3 (fold plan 2.4), Linux x86_64.
#
#   scripts/fold/run_equivalence.sh REFERENCE_DIR [BUILD_DIR]
#
# REFERENCE_DIR is the materialized v1.0.11 reference (final-sources.json,
# shipped-compdb.json, depscan.json, configure-linux.sh, codemodel/; see
# docs/fold-in/REFERENCE.md). It holds ROM-derived payload paths, so it lives
# on the devops machine, never in git. BUILD_DIR defaults to
# build/fold-equivalence. Everything that compiles runs in the build image
# (DKR_EQUIVALENCE_IMAGE, default: the exact image the v1.0.11 reference was
# built with, by image ID; docker/ci-linux-full/Dockerfile is its recipe).
#
# Steps: check the reference against docs/fold-in/REFERENCE.md and its own
# pins (it is mounted read-only; its scan and preprocess caches go to
# BUILD_DIR/reference-cache), configure this tree with the reference's options
# minus the release chain's, check the dependency stage patches reproduce the
# shipped third-party files (dependency_stages.py), check
# scripts/prepare_payloads.py reproduces the ROM-derived inputs the reference
# was configured with (prepared; it reads this tree's build/generated and
# build/decomp, so BUILD_DIR must lie under build/), then compare per-source
# compile settings and link lines (targets), TU sets, header closures and file
# bytes (level 1), preprocessed tokens plus compile flags (level 2), and
# objects for the TUs level 2 lets differ because of a header variant
# (level 3, always run). Exit status is non-zero on any unexplained
# difference; scripts/fold/allowed-deltas.json, header-variants.json and
# shader-pending.json list the explained ones. BUILD_DIR/equivalence/
# fold-equivalence.json records the run for signing (docs/fold-in/REFERENCE.md).
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
reference="$(cd "${1:?usage: run_equivalence.sh REFERENCE_DIR [BUILD_DIR]}" && pwd)"
build="${2:-$root/build/fold-equivalence}"
image="${DKR_EQUIVALENCE_IMAGE:-sha256:a40e107015464b92bff4e383545938f206590f73a6d1302516cef012e4f66439}"
jobs="${DKR_EQUIVALENCE_JOBS:-12}"
tool="$root/scripts/fold/equivalence.py"
out="$build/equivalence"
# The reference is mounted read-only; its scan and preprocess caches live here.
cache="$build/reference-cache"
mkdir -p "$out" "$cache"

# A worktree's .git points into the main checkout; the configure reads it.
git_common="$(cd "$root" && cd "$(git rev-parse --git-common-dir)" && pwd)"
mounts=(-v "$root:$root" -v "$reference:$reference:ro" -v "$git_common:$git_common:ro")
while IFS= read -r path; do
  [ -n "$path" ] && mounts+=(-v "$path:$path:ro")
done < <(python3 "$tool" options --reference "$reference" --mounts)
# The pinned image lacks libcurl's headers; the reference configure installed
# them the same way.
in_image() {
  docker run --rm --platform linux/amd64 "${mounts[@]}" -w "$root" "$image" bash -c \
    "set -e; dpkg -s libcurl4-openssl-dev >/dev/null 2>&1 || (apt-get update -qq && apt-get install -y -qq libcurl4-openssl-dev) >/dev/null 2>&1; git config --global --add safe.directory '*'; $1"
}

options=()
while IFS= read -r option; do
  options+=("$option")
done < <(python3 "$tool" options --reference "$reference")
printf -v configure '%q ' cmake -S "$root/runtime-recomp" -B "$build" -G Ninja "${options[@]}"

echo "== reference"
python3 "$tool" verify-reference --reference "$reference"

echo "== configure (no overlay, no project include)"
mkdir -p "$build/.cmake/api/v1/query"
touch "$build/.cmake/api/v1/query/codemodel-v2"
in_image "$configure >'$out/configure.log' 2>&1 || { tail -30 '$out/configure.log'; exit 1; }"

echo "== shipped compile commands and header scan"
in_image "python3 -B '$tool' shipped --build '$build' --out '$out/shipped-compdb.json' \
  && python3 -B '$tool' depscan --compdb '$out/shipped-compdb.json' --build '$build' --out '$out/depscan.json' --jobs $jobs"
ref_build="$(python3 "$tool" options --reference "$reference" --reference-build)"
if [ ! -f "$cache/depscan-by-target.json" ]; then
  in_image "python3 -B '$tool' depscan --compdb '$reference/shipped-compdb.json' --build '$ref_build' --out '$cache/depscan-by-target.json' --jobs $jobs"
fi

status=0
rm -f "$out"/record-*.json "$out/fold-equivalence.json"
echo "== closure (ROM-free level 1 subset, as CI runs it)"
python3 "$tool" closure --check || status=1
echo "== prepared inputs (scripts/prepare_payloads.py from this tree)"
if python3 -B "$root/scripts/prepare_payloads.py" --output "$build/prepared" >"$out/prepare.log" 2>&1; then
  python3 "$tool" prepared --reference "$reference" --prepared "$build/prepared" \
    --record "$out/record-prepared.json" || status=1
else
  tail -20 "$out/prepare.log"
  status=1
fi
echo "== dependency stages"
python3 "$root/scripts/fold/dependency_stages.py" --reference "$reference" --build "$build" --check || status=1
echo "== targets"
python3 "$tool" targets --reference "$reference" --folded-build "$build" --record "$out/record-targets.json" || status=1
echo "== level 1"
python3 "$tool" level1 --reference "$reference" --folded-build "$build" \
  --folded-compdb "$out/shipped-compdb.json" --folded-deps "$out/depscan.json" \
  --reference-deps "$cache/depscan-by-target.json" --record "$out/record-level1.json" || status=1

echo "== level 2"
rm -rf "$out/pp"
in_image "python3 -B '$tool' preprocess --compdb '$out/shipped-compdb.json' --build '$build' --out '$out/pp' --jobs $jobs"
if [ ! -d "$cache/pp" ]; then
  in_image "python3 -B '$tool' preprocess --compdb '$reference/shipped-compdb.json' --build '$ref_build' --out '$cache/pp' --jobs $jobs"
fi
python3 "$tool" level2 --reference "$reference" --folded-build "$build" \
  --folded-compdb "$out/shipped-compdb.json" --reference-pp "$cache/pp" --folded-pp "$out/pp" \
  --record "$out/record-level2.json" || status=1

echo "== level 3"
rm -rf "$out/level3"
in_image "python3 -B '$tool' level3 --reference '$reference' --folded-build '$build' \
  --folded-compdb '$out/shipped-compdb.json' --reference-pp '$cache/pp' --folded-pp '$out/pp' \
  --out '$out/level3' --record '$out/record-level3.json'" || status=1

echo "== evidence"
records=("$out"/record-*.json)
python3 "$tool" evidence --out "$out/fold-equivalence.json" --image "$image" "${records[@]}" || status=1
exit "$status"
