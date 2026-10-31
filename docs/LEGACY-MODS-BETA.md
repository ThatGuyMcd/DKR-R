# Custom tracks, characters, vehicles and the Mod Store — v1.0.12

Open **Mods / Hacks** to manage tracks, characters, Vehicles and Custom Magic Codes.
The **Mod Store** brings the library into the launcher. Choose grid view or
the detailed browse-and-inspect view, then search/filter the catalogue.

## Install and update

One Install action downloads, verifies and imports a package. Review code
permissions before enabling executable mods. A webpage or incomplete archive
is not a mod package and is not installed as one.

The Store keeps installation receipts, so a managed item can show Installed,
Update or Repair. Duplicate candidates offer a choice rather than silently
creating another copy. Updates replace the selected managed installation;
compatible settings are retained, and changed executable code needs approval.
Same-name matches alone are not proof that two mods are identical.

Use the Store's installed/update pages for managed content. Imports made
outside the Store may need an explicit duplicate/replacement choice. Backups
and interrupted-install recovery protect owned files; do not delete receipts
or recovery folders while diagnosing an installation.

Discord sign-in provides account features, reviews and requests. Moderators
and admins see only the library actions their server-side role allows. A
launcher checkbox cannot grant moderation rights.

Online and the Mod Store now share one display name. You can reach the account
settings from **DKR-R Online → Online Profile** too. Picture choices, sharing,
remembered sign-in and friend cards are covered in the [Online guide](ONLINE_MULTIPLAYER.md#profile-pictures-and-friends).
Character entries show portraits using the same compact preview style as vehicles.

## Vehicles

Import supported `.dkrkart` cars directly or from ZIPs, or install from the
Store's Vehicles category. Managed cars use the same receipts/Update/Repair
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
