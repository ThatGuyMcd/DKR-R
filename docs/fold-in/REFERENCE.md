# Fold reference (v1.0.11, Linux x86_64)

The fold reproduces tag `Version1.0.11` (`6bdbcde`). The reference is a
materialization of that tag: an unmodified clone, the capsule restored with
`patches/upstream-sync/restore_capsule.py`, and a Linux configure of the full
48-layer chain (`release-prep/v1.0.11/ReleaseOverlay.cmake`) in the
`dkr-ci-linux-full` image. The owned, generated and code-policy inputs are
the ones that reproduce the shipped owned IDs, so
`generated/memory-stability/contracts.json` holds

- V77 `5e22da8e6095b362ede17c69a2341aae2df24cbf8561c3af57fc86566d11784e`
- V80 `32c82fea3111440434926d5782e174c9f2d036d198f039c5424393a835f948b0`

Options: `DKR_RELEASE_VERSION=1.0.11`, `DKR_RUNTIME_BUILD_RT64=ON`,
`DKR_RUNTIME_BUILD_SDL3_INPUT_HOST=ON`, `DKR_NETPLAY_WEBRTC=ON`,
`DKR_EXPERIMENTAL_RACE_TEST=ON`, every `*_QUALIFICATION=OFF`, plus the payload
paths. These are still assumptions until the maintainer's per-platform
`CMakeCache.txt` confirms them (plan open question 11).

The final-state dump (`targets.tsv`) runs after every layer's deferred calls
and covers all 267 targets. It also lists per-source include directories,
options, definitions and `LANGUAGE`/`SKIP_*`. The CMake File API codemodel of
the same configure is kept beside it.

The ROM-derived payloads can't leave the devops machine, so the reference
lives in the session scratchpad (`fold-in/reference-v1011/`), not in git.

| File | sha256 |
|---|---|
| `final-sources.json` | `b1724fec0c8ff8a9f985bb2f9da6ef0de6c5e13676a61684978f37d1e7a7ece3` |
| `targets.tsv` | `cf96e30e051582c01187575515892f79bf2ac90d1a3a716b7a6154e4124c8f11` |
| `shipped-compdb.json` | `f01e8c19728fb4d0e2050c02384efc07b5d6a160215d8ac823998c53ed4bb7f4` |
| `compdb-linux.json` | `95cc1dd8393ef29c29e9aa53fececd411526322999e938e5c43423f963cd21b0` |
| `depscan.json` | `4fd709eb1afa0d2cc848d7d2fa83d62713ea620d31d7b51f14677991e4a710bb` |
| `shipped-inputs.txt` | `de929d28697c71c5f63a60c1075841590db44bc9ff7243456f636f4a28629043` |
| `contracts.json` | `00c09f7ab7ac8541b0cf26ce6b64b4014737cbc7085a83ad8e81bd13e8f0a4f9` |
| `closure-files.tar` (every closure file at its staged path) | `e2d9bf779cb0821f778e60fe2d50798d265b7c9e918845a3b818b46912b48eda` |

Shipped closure: 1368 TUs behind `DKR-R`, `DKR-R-ModWorker`, `DKR-R-Updater`
and `DKR-R-InputHost`. Of these, 837 first-party files, 40 guest transforms,
6 guest-derived files and 11 dependency transforms. The first-party files are
written back by `scripts/fold/apply_closure.py` (step B1).

## Configure-time generation (deliberate)

Guest hooks (`cmake/GuestHooks.cmake`), the guest symbol headers
(`cmake/GuestSymbols.cmake`) and the Camera++ hook stubs (`cmake/Camera.cmake`)
run at configure time, like the existing code-mod dispatch staging. The plan
had build-time `add_custom_command`s; keep them at configure time. The gate's
header scan and preprocessing read the configured tree without building it,
so build-time outputs would not exist when it compares them.

## Evidence and signing (B5)

`scripts/fold/run_equivalence.sh` ends by writing
`build/fold-equivalence/equivalence/fold-equivalence.json`. It records the
commit, whether the work tree was clean, the image ID, this page's reference
sha256 table, the sha256 of every input the gate trusts (allowed deltas,
header variants, shader-pending list, closure record, patch manifest, guest
hooks, online identity), and each level's counts, allowed entries and
unexplained differences. The script exits non-zero unless every level passed
on a clean tree.

Steve signs it on the devops machine and attaches both files to the PR:

    ssh-keygen -Y sign -n fold-equivalence -f ~/.ssh/id_ed25519 fold-equivalence.json

Reviewers verify with the allowed-signers entry for Steve's key:

    ssh-keygen -Y verify -n fold-equivalence -f allowed_signers -I <signer identity> \
      -s fold-equivalence.json.sig < fold-equivalence.json

CI can't run levels 0-3 (they need ROM-derived payloads). The
`fold-equivalence` workflow runs the ROM-free part of level 1 instead:
`equivalence.py closure --check` compares every first-party closure file with
`scripts/fold/closure.json`, whitespace aside so the format-only commit (B9)
keeps it green. `closure --reference DIR --write` re-records it, and only a
gate run on the reference may justify that.

## The evidence chain (B9)

The fold is proven in two links, and Steve signs both records.

1. **v1.0.11 ≡ 8beac7c, the pre-B9 commit.** The full gate
   (`run_equivalence.sh`, levels 0-3 plus the prepared inputs) ran there for
   the last time, on a clean tree, and passed. Its `fold-equivalence.json` is
   kept as `fold-equivalence-pre-B9-signing-point.json` (sha256
   `ff9110e8b9cc6d936a2fea80ef93cf2133cdd8a6f97308113e607ef5e3b5cdbc`); that
   is the record the reference comparison ends with. The gate isn't rebaselined against later
   commits: reformatting moves line numbers, which `__LINE__` (in `assert`)
   carries into preprocessed tokens and objects, so levels 1-3 can't compare
   a formatted tree with v1.0.11.
2. **8beac7c → 52e7ae4 (B9) changes layout only.** `scripts/format_sources.py
   --prove 8beac7c 52e7ae4` reads both trees from git and accepts each changed
   file only if it lexes to the same C/C++ tokens (no preprocessing, comments
   and whitespace dropped; `scripts/format_tokens.py`), has the same CMake
   words, has the same Python syntax tree, or is a digest-pinning script whose
   only change re-pins files the same commit reformatted. The record
   (`docs/fold-in/B9-format-proof.json`: 964 files, 925 of them C/C++ with
   identical tokens, 44 re-pins, nothing unproven) lists every re-pin. The
   range includes ef8ef5e, which only adds `// clang-format off/on` around
   four tests' macro-call lists (clang-format never settles on them).
   Rerunning `format_sources.py --write` on ef8ef5e, then re-pinning (every
   digest in a `PIN_HOLDERS` script that is the sha256 of a reformatted
   file's old bytes becomes the sha256 of its new bytes, the mapping `--prove`
   checks), gives 52e7ae4.

After B9, CI keeps the ROM-free closure check (`closure_digest` is
whitespace-insensitive, so it survives the format pass) and the online
identity test. Later runtime changes are ordinary reviewed code.

No `__LINE__` or `__FILE__` expansion reaches netplay serialisation, hashing,
identity or replay data: first-party sources use neither macro, and the only
`assert` outside tests is in the renderer (`f3ddkr_rt64.cpp`).
