# DKR-R Online — v1.0.12

Open **DKR-R Online** in the existing launcher. Public/Friends browsing,
Private/Public hosting, Discord sign-in and a total online count sit alongside
the existing lobby controls. There is no separate website page to use.

Races and minigames support up to four racing ports; Adventure supports two
racers. A spectator-capable lobby can also have eight spectators. Everyone
needs a compatible complete v1.0.12 package, supported original ROM revision,
admitted gameplay rules and verified session mods. Keep `assets` and private
workers with the installation.

## Connect and find a lobby

1. Select your original ROM and local input profile. Your local controller
   mapping is independent of the online player number.
2. Reuse your existing Mod Store Discord account or select **Sign in with
   Discord** in Online. Finish the normal browser pairing flow.
3. Connect Online. **Public** lists worldwide public lobbies; **Friends** keeps
   the friend/invitation list. Public cards offer **Request to join** or
   **Spectate**; friend/code joining also has **Racer / Watch Race** selection.
4. Request a place. The host approves ordinary requests. Required mod/code
   consent, preparation and save/compatibility checks finish before admission.

Public discovery and aggregate presence need Discord sign-in. Offline play
does not. Existing private code/friend workflows remain available where their
normal transport/account rules permit. An already running legacy private
lobby is not silently published when the service reconnects: connect first,
then recreate it if you want public hosting.

The count represents distinct connected signed-in accounts across launcher
instances, including players in private rooms anonymously. It is not the
number of racing controller ports or every person playing offline. One account
on several launchers counts once; a crash or sleeping device can remain counted
until its lease expires, normally within 60 seconds. **Disconnect Online**
removes this instance. See [public lobbies and privacy](PUBLIC_LOBBIES.md).

## Profile, pictures and friends

Open **Online Profile** for your shared display name, picture and account
settings. There is one name for Online, friends and the Mod Store. Edit it and
choose **Save profile**. An active lobby keeps its current name until you leave;
failed saves keep your draft so you can retry. Ordinary text fields use a
case-readable font. The Discord username is no longer repeated underneath.

**Change profile picture** previews a PNG or JPG before saving. You can also
choose your Discord image or initials. Picture sharing is optional; friends
and lobby players receive the selected picture only while sharing is enabled.
Sign out and back in once after deploying the profile API to import Discord's
name and image metadata. The launcher offers separate choices for each.

Privacy and Share a friend code sit below the profile header. Account settings
contains **Keep me signed in**, sign-out, privacy information and the existing
confirmed library-account deletion. Reviews and requests stay in the Mod Store.
Deleting the library account does not delete your Discord account or local mods.

The Friends tab uses compact cards with a picture, name, status and actions.
**Add a friend** is above Search. Existing filters and sorting still work.
Lobby player cards offer **Add friend** for eligible players who are not already
friends, including other clients. Requests still need acceptance; sharing a
lobby does not automatically add anyone.

## Joining with mods

The Session mods dialog shows what is happening, how long it has taken,
and how much data was downloaded or reused. A busy host can queue your
request. Temporary transfers can be repaired up to five times; if a join
fails, use **Retry Join** or **Copy Join Diagnostics**. Verified completed
downloads can be reused when you retry. A first join with lots of mods
can still take longer while they are downloaded and prepared. Transfers and
initial checkpoints have more capacity, verified cached content is reused and
mod writes avoid flushing every chunk. Preparation still verifies complete files.
Use **Copy Join Diagnostics** to distinguish network transfer from preparation;
there is no fixed speed guarantee for every host or connection.

You can leave **Keep downloaded mods for offline play** enabled. Matching
installed copies are reused, and missing selected content is kept as
inactive entries after lobby admission. Your existing names, enabled
mods and saves stay as they are. A real content conflict is reported
without replacing the existing files or stopping lobby entry.

In Open Lobbies, **Join as** and **Browse lobbies** sit under your account
on wide windows. **Before you race** sits below **Your online view**. On
narrow windows, the lobby filters remain above the list.

## Host a lobby

1. Enable the mods you want before hosting. In **Host settings**, choose racer
   capacity, menu ownership, input delay/prediction and spectator support.
2. In **Host / Quick Join**, name the lobby, choose the Online Adventure save
   source and select **Private** or **Public**. Private is the default.
3. Create the lobby. Public rooms appear to other connected launchers. Private
   rooms use the existing friend list/invitations or lobby code.
4. Review pending guests. Friends' valid authenticated invitations retain their
   existing admission behaviour; a public row or ordinary code is not approval.
5. Racers complete preparation and choose **READY TO RACE**. Spectators do not
   take a racing port and do not need to ready as racers.
6. The host can run **Check Rollback connection**, review the Scoreboard, then
   start the synchronised countdown.

Host rules and the enabled mod catalogue are frozen for the session. Changing
the mod set requires recreating the lobby. The host guides shared menus until
Player Select by default; online slots receive their correct native players
without remapping physical controllers to local ports.

The public host mode is **Rollback**. It uses the existing owned replay backend
internally named `ExperimentalRollback`. The old Auto, Host Prediction and
Strict Input Sync selector is no longer offered for v1.0.12 public hosting.
Old wire enum values and historical investigations have not been reinterpreted.
Do not change configuration files to bypass the supported-mode gate.

The waiting lobby places the save/mod summary under its title and visibility
under the lobby code on wide windows. **Close lobby** removes the hosted room
without restarting the launcher. Your account can stay Online. A crash or lost
connection still uses the directory lease expiry.

## Rollback connection Scoreboard

**Check Rollback connection** takes about ten seconds while the lobby is
waiting. Everyone receives the same completed host-measured report, with their
own YOU marker. Select a row for details; controller A opens details and B
closes details before closing the report. The host can rerun the check.

| Score | Estimate |
|---|---|
| 9–10 | Excellent |
| 7–8 | Good |
| 5–6 | Mixed |
| 3–4 | Poor |
| 1–2 | Very poor |
| — | Not rated; insufficient, incompatible or locally overloaded measurement |

The racing grade is the weakest measured remote racer route. Any unrated racer
leaves the racing assessment incomplete. The host's row is a shared estimate,
not a fabricated self-ping score. Spectator playback grades never lower the
racing grade. Role/roster or tested-setting changes make an old report stale.

Grades describe a short network sample, not hardware FPS, checkpoint download
speed or a guarantee against desync. No settings change automatically. Start
with automatic input delay; the prediction window is adjustable from 2–20
frames, with ten frames a useful starting recommendation for distant players.
More prediction can increase replay work. A small window can run out before
actual inputs arrive. Changes apply to the next agreed lobby rules.

## Spectators and player swaps

Turn on **Spectator-capable lobby** before creating the room. Guests choose
**Watch Race**. The host can use **Make spectator / Make racer** while waiting;
making a racer requires a free racing place and clears Ready.

Spectators can view **All splits**, **Follow player** or the **T.T.-style
director**. Original vehicles can use its cinematic view; custom GPU-rendered
cars use safe racer cameras. D-pad left/right changes racer and C-up changes
camera mode. Spectator controls never become racing input.

Racers can offer a place and spectators can request one. The host approves
under **Player swaps**. The handover uses an agreed confirmed checkpoint;
there can be a short pause. The replacement inherits that racer and race
progress, with character/vehicle changes waiting for ordinary selection screens.
The outgoing racer becomes a spectator and is not an input relay.

Joining a running race as a spectator through the directory requires the
separate service flag to be enabled. It still needs a committed match
descriptor, compatible content and checkpoint catch-up. It never enables
late racer entry. A slow spectator is outside racing input/scene agreements
and can request a fresh viewing checkpoint. See [Spectators](SPECTATORS.md).

## Returning, leaving and interruptions

The host can return everyone to the lobby without closing it. The retained
party is advertised again only when the native runtime is genuinely waiting.
Checkpoints use one monotonic pulse time through receive/expiry processing;
the previous repeated zero-byte catch-up/start loop is repaired.

A spectator leaving does not end the race. Loss of a required racer returns
the match safely to the lobby rather than continuing with a missing input
owner. Closing the host closes the room. There is no host migration.

If the directory is unavailable, already admitted native peers can still
ready, check the connection, start, race, return or leave. Discovery and new
admissions recover separately. Reconnecting must not reset an approved long
mod transfer. Changes to privacy, lock, phase, invitation/key or capacity can
revoke admission; an ordinary roster/account refresh does not revoke approved
preparation by itself.

## Mods, saves and achievements

The host is the Mod Master. Its enabled tracks, characters, cars and code
settings form the frozen session identity. Clients review the required set,
approve executable code and consent to preparation. Matching installed data
or verified caches are reused, including locally disabled copies; only missing
payloads need transfer. Downloading, reconstruction against your own ROM and
verification must all finish. Cached names alone do not prove compatibility.

Verified received content can remain as inactive library entries. It does not
replace your offline activation choices. Custom cars are visual replacements
but their catalogue and per-racer choices are part of admitted online state.
No ROM or personal save collection is shared as a mod download.

The host owns persistent online Adventure progress. Continue, copy personal
progress and start fresh remain explicit choices. Clients and spectators use
session-owned copies; they do not overwrite personal Adventure or Controller
Paks. Only confirmed rollback progress can persist. Watching and development
sessions do not award gameplay achievements. See [Saves](SAVES.md),
[Vehicles](VEHICLES.md) and [Magic Codes](MAGIC_CODES.md).

Two-player Adventure retains its original shared hub and scene-specific
controller topology. A one-viewport hub does not mean a player left. Three- or
four-racer Adventure is not offered. Development tools need explicit consent
and host authority and cannot silently turn an ordinary lobby into a debug session.

## Local view and presentation

**Your online view** offers **Original split-screen** or **My racer fullscreen**
for eligible races, with local name/position labels, sizes and fading. Shared
menus, results, cutscenes and Adventure hubs keep their normal layout. When
ownership is incomplete, presentation falls back to the original view.

Input profiles, texture packs, renderer settings and master volume stay local.
Simulation-affecting camera/detail values and admitted code settings cannot
be changed privately. General independent high-LOD/scenery parity is not
qualified by this release. Fullscreen does not remove the work of simulating
other racers. See [Graphics](GRAPHICS.md) and [Sound](SOUND.md).

## Transport, compatibility and support

The website API provides worldwide discovery, account presence and bounded
admission. PeerJS/STUN still establishes the existing direct WebRTC connection;
Rollback carries the match. The website does not relay gameplay frames. There
is no bundled TURN service, voice/text chat or universal NAT traversal.
Restrictive firewalls/mobile networks can still block a direct route, and peers
can see each other's public network addresses.

Native X25519 key agreement, XChaCha20-Poly1305 authentication and WebRTC DTLS
protect admitted traffic. Public rows contain no reusable lobby code or
credentials. The native host remains authoritative for controller ports,
roles, lock, mod/save consent, Ready and start.

Admission checks build/protocol, linked replay engine, ROM, Patch Pipeline,
gameplay/Magic Codes, prepared resources, save/checkpoint proof, architecture
and floating-point policy. Matching engine IDs alone do not prove that a mixed
x64/ARM64 session is admitted or hardware-qualified. Do not bypass a mismatch.

For a failure, keep host and client logs from the same attempt, package names,
ROM revision, racer/spectator count, enabled mods, delay/window and exact scene.
Distinguish input waits, replay work, loading and rendering. Remove private
codes, credentials and paths before sharing. [Troubleshooting](TROUBLESHOOTING.md)
lists the relevant recovery steps and log locations.
