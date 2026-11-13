# Custom tracks, characters, vehicles and the Mod Library

Open **Installed Mods** to manage tracks, characters, Vehicles and Custom Magic Codes.
The **Mod Library** has its own sidebar button above **Installed Mods**, in the
launcher and in-game overlay. Browse the card grid, or use search, categories
and sorting to find a mod. Open a card for its details, comments and downloads.

## Install and update

One Install action downloads, verifies and imports a package. Review code
permissions before enabling executable mods. A webpage or incomplete archive
is not a mod package and is not installed as one.

The Library keeps installation receipts, so a managed item can show Installed,
Update or Repair. Duplicate candidates offer a choice rather than silently
creating another copy. Updates replace the selected managed installation;
compatible settings are retained, and changed executable code needs approval.
Same-name matches alone are not proof that two mods are identical.

Use the Library's installed/update pages for managed content. Imports made
outside the Library may need an explicit duplicate/replacement choice. Backups
and interrupted-install recovery protect owned files; do not delete receipts
or recovery folders while diagnosing an installation.

Discord sign-in provides account features, reviews and requests. Moderators
and admins see only the library actions their server-side role allows. A
launcher checkbox cannot grant moderation rights.

Online and the Mod Library share one display name. You can reach the account
settings from **DKR-R Online → Online Profile** too. Picture choices, sharing,
remembered sign-in and friend cards are covered in the [Online guide](ONLINE_MULTIPLAYER.md#profile-pictures-and-friends).
Character entries show portraits using the same compact preview style as vehicles.

## Vehicles

Import supported `.dkrkart` cars directly or from ZIPs, or install from the
Library's Vehicles category. Managed cars use the same receipts/Update/Repair
workflow. Enable them before Track Select; Vanilla remains available. Custom
cars change visuals, not handling, and Adventure does not use them. Hover/plane
formats are reserved. The stored category ID remains `legacy` for compatibility.
See [Vehicles](VEHICLES.md).

## Track and character imports

Legacy patches are extracted into supported native assets. Dedicated behaviour
adapters cover reviewed changes; unknown executable/global ROM edits are not
automatically executed. The importer describes support in Details.

Modern `.dkrmap` tracks can be turned on/off like legacy tracks. ZIP packages
must contain a complete course, including declared music/HD assets. A single
course inside wrapper folders is accepted; ambiguous multi-course archives
need separate imports. See [Custom tracks](CUSTOM_TRACKS.md).

Track Select uses six columns for custom tracks. Custom cups appear below
them when their code mod and required tracks are enabled. Custom character
portraits include supported extracted portrait sources and the in-level
artwork in Fire Mountain and Smokey Castle; that artwork is not the minigame HUD.

Offline mod choices and saves are not replaced by a host's online activation.
The host selects the exact online content/settings and clients consent before
verified preparation. Existing matching installed data can be reused. See
[Online](ONLINE_MULTIPLAYER.md), [Magic Codes](MAGIC_CODES.md) and [Saves](SAVES.md).

## Player Select names

Only selected custom characters receive an added name, drawn with the native
colourful font. The upper two stage slots place names above the models and the
lower two below their feet. There is no added player-number plate or global
Choose your Racer footer. Up to four local players are supported; players
sharing one character share its label.

Names use a bounded two-line area, shortening lower-row text to one line when
needed. Each physical slot has its own visible area so long names, model sizes
and paging do not push another selected label off screen. Native L/R hints
appear only when that selected actor can be browsed. Unselected custom actors
keep the original stage presentation.

## Battle maps

Track Lab battle courses require valid mode objects and connected AI paths.
The v1.0.15 release does not turn an arbitrary course into a supported battle
map just by changing its type. See [Track Lab authoring](../tools/blender/README.md#battle-course-authoring-limits).
