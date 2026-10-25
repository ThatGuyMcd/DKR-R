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
# (DKR_EQUIVALENCE_IMAGE, default dkr-ci-linux-full:latest).
#
# Steps: configure this tree with the reference's options minus the release
# chain's, then compare per-source compile settings and link lines (targets),
# TU sets, header closures and file bytes (level 1), preprocessed tokens plus
# compile flags (level 2), and objects for the TUs level 2 lets differ because
# of a header variant (level 3; DKR_EQUIVALENCE_LEVEL3=0 skips it, it compiles
# them twice). Exit status is non-zero on any unexplained
# difference; scripts/fold/allowed-deltas.json and header-variants.json list
# the explained ones.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
reference="$(cd "${1:?usage: run_equivalence.sh REFERENCE_DIR [BUILD_DIR]}" && pwd)"
build="${2:-$root/build/fold-equivalence}"
image="${DKR_EQUIVALENCE_IMAGE:-dkr-ci-linux-full:latest}"
jobs="${DKR_EQUIVALENCE_JOBS:-12}"
tool="$root/scripts/fold/equivalence.py"
out="$build/equivalence"
mkdir -p "$out"

# A worktree's .git points into the main checkout; the configure reads it.
git_common="$(cd "$root" && cd "$(git rev-parse --git-common-dir)" && pwd)"
mounts=(-v "$root:$root" -v "$reference:$reference" -v "$git_common:$git_common:ro")
while IFS= read -r path; do
  [ -n "$path" ] && mounts+=(-v "$path:$path:ro")
done < <(python3 "$tool" options --reference "$reference" --mounts)
in_image() {
  docker run --rm --platform linux/amd64 "${mounts[@]}" -w "$root" "$image" bash -c \
    "set -e; (apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq libcurl4-openssl-dev) >/dev/null 2>&1; git config --global --add safe.directory '*'; $1"
}

options=()
while IFS= read -r option; do
  options+=("$option")
done < <(python3 "$tool" options --reference "$reference")
printf -v configure '%q ' cmake -S "$root/runtime-recomp" -B "$build" -G Ninja "${options[@]}"

echo "== configure (no overlay, no project include)"
mkdir -p "$build/.cmake/api/v1/query"
touch "$build/.cmake/api/v1/query/codemodel-v2"
in_image "$configure >'$out/configure.log' 2>&1 || { tail -30 '$out/configure.log'; exit 1; }"

echo "== shipped compile commands and header scan"
in_image "python3 -B '$tool' shipped --build '$build' --out '$out/shipped-compdb.json' \
  && python3 -B '$tool' depscan --compdb '$out/shipped-compdb.json' --build '$build' --out '$out/depscan.json' --jobs $jobs"
ref_build="$(python3 "$tool" options --reference "$reference" --reference-build)"
if [ ! -f "$reference/depscan-by-target.json" ]; then
  in_image "python3 -B '$tool' depscan --compdb '$reference/shipped-compdb.json' --build '$ref_build' --out '$reference/depscan-by-target.json' --jobs $jobs"
fi

status=0
echo "== targets"
python3 "$tool" targets --reference "$reference" --folded-build "$build" || status=1
echo "== level 1"
python3 "$tool" level1 --reference "$reference" --folded-build "$build" \
  --folded-compdb "$out/shipped-compdb.json" --folded-deps "$out/depscan.json" || status=1

echo "== level 2"
rm -rf "$out/pp"
in_image "python3 -B '$tool' preprocess --compdb '$out/shipped-compdb.json' --build '$build' --out '$out/pp' --jobs $jobs"
if [ ! -d "$reference/pp" ]; then
  in_image "python3 -B '$tool' preprocess --compdb '$reference/shipped-compdb.json' --build '$ref_build' --out '$reference/pp' --jobs $jobs"
fi
python3 "$tool" level2 --reference "$reference" --folded-build "$build" \
  --folded-compdb "$out/shipped-compdb.json" --reference-pp "$reference/pp" --folded-pp "$out/pp" || status=1

if [ "${DKR_EQUIVALENCE_LEVEL3:-1}" != 0 ]; then
  echo "== level 3"
  rm -rf "$out/level3"
  in_image "python3 -B '$tool' level3 --reference '$reference' --folded-build '$build' \
    --folded-compdb '$out/shipped-compdb.json' --reference-pp '$reference/pp' --folded-pp '$out/pp' \
    --out '$out/level3'" || status=1
fi
exit "$status"
