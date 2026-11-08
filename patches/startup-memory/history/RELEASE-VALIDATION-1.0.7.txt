# v1.0.7 release build record

20 October 2026. This records this exact stable delivery, not earlier test counts.

## What was done

- Windows x64, Linux x86_64, Linux ARM64 and Android ARM64 compiled and linked.
- Steam Deck uses the exact final Linux x86_64 AppImage bytes.
- PE/ELF/APK versions, architectures, archive integrity, linked feature markers and ownership membership inspected.
- Android: application ID `io.github.thatguymcd.dkrr.preview`, code `1070018`, API 28+, ARM64. Its signer matches both the published 1.0.6 APK (code 1060002) and the last beta (1070017). The existing distributed debug certificate was retained; no signing identity was replaced.
- Both Linux architectures require GLIBC 2.38 and GLIBCXX_3.4.32. Host C++/graphics/audio/font libraries are intentionally external under the documented AppImage policy; no unexplained linked dependency remains.
- Source privacy/asset and relative-link inspection: 2000 files, zero flagged issues and zero missing checked relative links.
- The SDK 2 kit matches the corrected builder/public headers and keeps API 1 support. The updated 64-page Bible was rendered and visually reviewed; static text bounds reported zero overflow.
- No offline Player Guide or full SDK is inside the game packages. Required legal notices remain. Desktop packages retain only the small checksum-pinned trophy updater needed by its existing UI action.
- Accepted water bytes were reconstructed into the release capsule. The owner-accepted beta ZIP remains unchanged. Fresh reconstruction and Windows configuration succeeded after old staging output was archived.
- Protected submodules, RecompiledFuncs and RecompiledPatches were not edited. All new authoring is in the Patch Pipeline; generated documentation/SDK publication is applied by that pipeline.

## Online identity

- US revision 77: `55e839f5e1ea9c06e1a1eeeba0aae45607b8c02f2dd89c6c7e343c42419ff62d` on Windows, Linux x86_64/ARM64 and Android.
- US revision 80: `57552c9d45a9d1f715dd734ee6ae6608a237bde47e22b39a44fbc8bf984440f5` on Windows, Linux x86_64/ARM64 and Android.

## What was NOT run or claimed

No game, launcher, code mod, CTest, unit suite, replay, multiplayer match or performance benchmark was executed for this delivery. Compiler/toolchain probes and archive/document/source inspection are not gameplay acceptance.

The owner accepted the HD-water startup correction on the preceding Windows package. That is not blanket acceptance of this exact stable package on every platform. Actual GPU, controller/touch, saves, mod combinations, cross-platform online, background/resume and sustained-performance checks remain with the owner.

Independent online maximum-LOD/scenery parity remains unqualified. Existing baseline graphics source/build switches were preserved, not reintroduced as a newly fixed feature. VR and the withdrawn atmosphere system are not release features. Apple source ports are retained, but no macOS/iOS binary was built.

## Cleanup and recovery

249 obsolete output locations, 55282 files, 10162854524 bytes were moved and destination-hash verified. Nothing was permanently deleted.

Backup: `F:/DKR Dist dump/v1.0.7-release-20261020/obsolete-output`. BEFORE-MOVE.json, REMOVE-LIST.txt and MOVE-JOURNAL.json record exact recovery paths. The earlier source snapshot and first cleanup remain separate. Active prepared inputs, protected checkouts and link-containing output trees were retained.

## Updating and publishing

Existing 1.0.7+beta version readers compare equal to stable 1.0.7: upgrade those copies manually. Automatic-update signatures were not generated because the existing private release signing key has not been located. The public trust anchor was not replaced. Manual release assets are complete.

The source is prepared on a local release branch. No push to main, tag, GitHub release or asset upload was performed in this build pass. Owner gameplay acceptance and publishing authority remain separate.

## Final upload hashes

- `DKR-R-1.0.7-Android-arm64.apk`: `bd8f8ac993794d589704a2b3711f3f914be6a9b47731e55017a12932336bbf8a` (47428076 bytes).
- `DKR-R-1.0.7-Linux-aarch64.AppImage`: `c312b41f41cf493c2bf5fea1eb71d8e9f23089924968f069fea828207b212d24` (58063368 bytes).
- `DKR-R-1.0.7-Linux-x86_64.AppImage`: `2de549f88ad53052aa892ddd2a1ed77ecdd03ed88ac976509443c9f83dc31b08` (67348984 bytes).
- `DKR-R-1.0.7-SteamDeck-x86_64.AppImage`: `2de549f88ad53052aa892ddd2a1ed77ecdd03ed88ac976509443c9f83dc31b08` (67348984 bytes).
- `DKR-R-1.0.7-Windows-x64.zip`: `1c3504a40417cfdfa96977d00f6a2a499394bd0c2e9522303fe4edb08cac0842` (58503709 bytes).
- `DKR-R-Modding-SDK-2.0-v1.0.7.zip`: `0bfa64dd5aeb3496f12dd8836cf1d47e8e18b6b742b6b9d0260959d848e90bee` (614555 bytes).
- `README.txt`: `e07623af8b7c29711285142cd6e6780e4536b81d9270def70d3c1045d8d048d8` (1153 bytes).
- `RELEASE-NOTES.txt`: `c5e97b2c29cd0ae333c13ebae9e2caa01378d8d55ff7add43b67ec6b47908518` (11246 bytes).
- `track_lab.zip`: `d98c3cd10f8ca9118bea6b6db189944a0af2a722a3253bf5fa08a9ce076803b2` (661937 bytes).

## Private evidence

Build/configuration, APK signer, ELF dependency, source/link, capsule and delivery records: `D:/DiddykongWorkFolder/release-v1.0.7-20261020/evidence`. Exact Windows EXE/PDB pairs are under its private-symbols folder, not in release downloads.

The preceding validation text is retained unchanged in [historical evidence](patches/release-prep/v1.0.7/history/RELEASE-VALIDATION-before-release.txt). Those old results do not become new results here.
