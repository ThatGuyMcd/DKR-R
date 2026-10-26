# Legacy mod import and asset-bank tests

## Current release composition

For a release use the [release steps](../../../docs/BUILDING.md#release-packages). The module notes below
explain this layer; their dated playtest paths are historical inputs, not the
current release command. The accepted water/source capsule preserves the
composed behaviour. Dependencies and generated guest checkouts stay read-only.

This suite checks the importer, reviewed behaviour contracts, private asset
banks, resident resources, catalogue/preparation and online content boundaries.
It is part of the current playable mod system, not just an import preview.

The original milestone notes are retained in [HISTORY.md](HISTORY.md).
Their “not implemented” lists describe those earlier checkpoints, not the
current runtime. Current user instructions are in
[the mod guide](../../../docs/LEGACY-MODS-BETA.md).

## Build

Run from the repository root with a separate build directory:

```text
cmake -S runtime-recomp/tests/legacy_mods -B <build-directory> -DCMAKE_BUILD_TYPE=Release -DDKR_DEPENDENCIES_DIR=<absolute-repository-path>/extern
cmake --build <build-directory> --config Release --parallel 4
ctest --test-dir <build-directory> -C Release --output-on-failure
```

On Visual Studio generators executables are under Release. Use the test build
directory for temporary fixtures, not the source tree.
This standalone suite complements the complete configured runtime tests;
it does not prove GPU, gameplay or real-WAN compatibility.

`DKROnlineModProfileTests --installed-benchmark <canonical-v77-rom>
<canonical-v80-rom> <legacy-library> <worker>` prints the manifest, prepared
resource and boot-bank identities. Run it on each platform and compare the
values, not timings or paths. It checks that source catalogues stay unchanged
and that matching installed sources need no importer worker.

## Private corpus checks

Corpus tests require privately supplied original ROMs and patch files.
Never commit or distribute those inputs with tests:

```text
DKRLegacyModCorpusTests <absolute-worker-path> <owned-v77-rom> <owned-v80-rom> <bigboo-zip> <haunter-zip> <community-pack-zip>
DKRLegacyModInspect --assets <owned-rom> <us.v77-or-us.v80>
DKRLegacyModInspect --geometry <owned-rom> <us.v77-or-us.v80>
```

These are developer inspection tools, not an alternate player launcher.
Resource limits are not a complete OS security sandbox. Native adapters must
be reviewed and identity-bound; accepting an archive does not grant its code
execution rights.

## Production boundary

Normal and owned payloads both need composed production hooks.
Private qualification flags remain off for release packages.
No test failure authorises editing generated functions, submodules, checksums
or signature checks to force admission.
See [Building](../../../docs/BUILDING.md) and [Development](../../../docs/DEVELOPMENT.md).
