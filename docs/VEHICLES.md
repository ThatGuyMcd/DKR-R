# Custom vehicles

v1.0.12 supports custom **cars** from Syeo's `.dkrkart` exports, either directly
or inside a ZIP. `.dkrhover` and `.dkrplane` are reserved for future formats;
custom hovercrafts and planes are not supported by this release.

## Install and choose a car

1. Use **Mods / Hacks → Import mods → Custom vehicles**, or install a car from
   the Mod Store's **Vehicles** category.
2. Enable the imported entries under **Vehicles**.
3. Start **Track Select**, choose a character and the native Car vehicle type.
4. Scroll between **Vanilla** and enabled custom cars. Left/right changes car,
   A confirms and B goes back. Each player keeps their own choice; duplicates
   across racers are allowed.

Vanilla uses the existing vehicle for your chosen character. Custom cars change
vehicle visuals only; they do not supply a driver or new physics, collision,
handling or voices. Adventure does not use custom cars. Unsupported character
exports retain Vanilla safely rather than hiding guessed model geometry.

The native wooden picker, car portraits, steering, wheel rotation, seating and
LOD ownership are part of the accepted vehicle layer. Reimporting an identical
inner `.dkrkart` both directly and from ZIP does not create two installations.
Existing cars do not need reimporting just to use v1.0.12.

Enabled cars can be chosen for CPUs in Track Select. A CPU keeps its choice for
that race and can have another on the next race. This changes appearance, not
character difficulty or handling. Custom CPU characters are a separate option.

## Updated vehicle exports

Syeo's `original-resolution-v1` cars are supported in v1.0.12, including
Brute v2. Import the original `.dkrkart`; you do not need to rename it or
shrink its images. Larger PNGs are prepared as small material tiles once
during import, keeping their original pixels and the native memory limits.
This can add triangles. Exports exceeding the preparation budget still
receive an error rather than raising the renderer limits.

The explicit revision 1 driver seating contract uses raw native model
anchors. Older exports keep their existing placement rules. Source files,
archive hashes and installed copies are not rewritten by this support.

Disabled boost anchors use the native effect position. Enabled custom
boost anchors and unknown explicit revisions are not supported yet and
are rejected with a specific error. Custom hovercrafts and planes remain
unsupported.

## Online and managed installations

The host's enabled car catalogue is frozen for the lobby. Clients reuse exact
installed exports or prepare missing packages through normal consent and
verification. Per-racer choices are agreed Rollback state, not local texture
preferences. Recreate the lobby after changing its enabled catalogue.

Store-managed cars use installation receipts and the normal Update / Repair,
duplicate and recoverable transaction flows. Keep receipts and retained editions
when diagnosing an interrupted install. A same-name file is not proof of identity.

The website and launcher display **Vehicles**. The backend category identifier
remains `legacy` to preserve existing entry IDs, reviews, requests and file
mappings. Do not rename that stored ID or move hosted files during deployment.
Explicit historical ROM patch files retain their separate importer route.

See [mods and installation](LEGACY-MODS-BETA.md), [Online](ONLINE_MULTIPLAYER.md)
and [library administration](MOD_LIBRARY_ADMIN.md).
