# Custom character portraits — beta 5

## Current release composition

For v1.0.11 use the [release pipeline](../release-prep/v1.0.11/BUILDING.txt). The module notes below
explain this layer; their dated playtest paths are historical inputs, not the
current release command. The accepted water/source capsule preserves the
composed behaviour. Dependencies and generated guest checkouts stay read-only.

This stage fixes two different problems. Some character imports picked the
neighbouring stock portrait, and the portraits placed inside the battle arenas
still used the donor character rather than the racer playing that slot.

## What changes

The arena-crash correction explicitly sign-extends both portrait-chain pointers
before passing them to the generated `MEM_W` accessors. The original hook used
unsigned 32-bit addresses, which the ordinary CPU's KSEG0 rebasing interpreted
outside guest RAM. The 17 October crash log identifies the first of these reads
in `obj_loop_characterflag`. Aligned KSEG0 pointer guards now retain the stock
face if the presentation chain is unavailable. Both ROM revisions and the
ordinary/rollback payloads receive the same correction; all other original
arena instructions remain unchanged.

- The verified US 1.0 / US 1.1 portrait order is
  `122, 123, 124, 126, 125, 127, 128, 129, 130, 131`.
- New character preparations use that order. Existing v2 preparations recover
  the correct image from the retained, SHA-256-verified source review. Every
  frame is validated before entering the private asset namespace.
- Existing installed character IDs, enablement, catalogues and isolated save
  folders are not rewritten. There is no automatic re-import or save migration.
- Downloading an older frozen online character selection can reproduce that
  exact old artifact, then use the same authenticated portrait recovery at
  launch. Corrected and old preparations are never treated as interchangeable.
- In-world CharacterFlag signs, including Fire Mountain and Smokey Castle,
  resolve the participant's actual custom character. Different characters
  sharing a donor keep their own images. Stock racers retain stock portraits.
- The original sign geometry and its texture-size-based UV calculation remain
  intact. The minigame HUD is not changed.

If a source mod contains no custom portrait, it keeps the correct stock face.
The two installed source reviews for **007 Bond (car only)** and **Sexy Grunty**
did not contain changed portrait artwork. A different release may include it.
If retained artwork is missing or fails verification, the launch reports that
the character's source import needs preparing again; it does not invent art or
silently substitute unrelated mod files.

## Patch Pipeline only

`scripts/Prepare-CharacterPortraitPayload.ps1` takes the complete save-layout
beta 5 payload as read-only input and creates a NEW output directory:

1. `native/src` is a copy of first-party runtime inputs. Seven native files are
   changed by pinned transformations. All other copied files must remain
   byte-identical. Two unchanged source-relative symbol tables are copied
   separately under the fresh output's `modding` directory. This lets all
   consumers use the same extended private
   preparation-result ABI without touching runtime sources in place.
2. `generated-v77` and `generated-v80` change only CharacterFlag's texture
   choice, using the already-audited custom portrait callback.
3. `owned-v77` and `owned-v80` retain checked memory access, private callbacks,
   checkpoint ownership, and code-mod protection. Their manifests bind both
   the native overlay and the guest portrait hook to the online build contract.
4. `NativeOverlay.cmake`, selected using `CMAKE_PROJECT_INCLUDE`, verifies the
   stage and substitutes first-party source paths at configure time. It never
   rewrites dependency code or protected generated sources. The existing
   project-owned renderer support sources on RT64's link interface are rebound
   to the identical staged paths, avoiding duplicate executable objects.

Example generation:

```powershell
./scripts/Prepare-CharacterPortraitPayload.ps1 `
  -BaselinePipeline C:/DKRPort/build/save-layout-beta5-20261017-r2 `
  -OutputRoot C:/DKRPort/build/character-portraits-beta5-NEW
```

Configure both generated CPU directories, both owned directories and both
`DKR_CODE_POLICY` paths to the fresh output. Also set:

```text
CMAKE_PROJECT_INCLUDE=C:/DKRPort/patches/character-portraits/NativeOverlay.cmake
DKR_PORTRAIT_STAGE=<fresh-output>/native
DKR_RELEASE_VERSION=1.0.7+beta.5
BUILD_TESTING=OFF
```

Build Windows and Linux x86_64/AppImage. No game, mod execution, replay tests,
CTest or self-tests are run by this pipeline. Gameplay checks belong to the
user: missing portraits, arena signs, two custom racers sharing a donor, stock
racers, online host/client downloads, and unchanged existing save progress.
