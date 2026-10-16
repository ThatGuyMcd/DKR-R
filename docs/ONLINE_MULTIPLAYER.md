# DKR-R Online

Race with friends through Quick Join or friend invites. No separate launcher,
account or manual port forwarding is needed. Player 1 is the host; approved
clients become Players 2–4. Races and minigames support 2–4 racers, and Adventure
supports two.

Use the same complete DKR-R package on every machine, including its private
workers. The same original ROM revision, compatible build, gameplay rules and
session mod identity must be admitted before the game starts.

Version 1.0.7 uses protocol 50. Use 1.0.7 on every device, not a mixture with
1.0.5/1.0.6. Windows and Linux/SteamOS are not separate online versions.
Replay-engine IDs normalise text line endings, so separately generated Windows
and Linux builds of the same source agree. Actual code changes are still checked.
The lobby checks the replay engine, then compares the actual prepared mod
resources before admitting a modded client. A cached download is still checked;
there is no need to delete your offline library or download everything again.

## Starting a lobby

1. Select your original ROM and local input profile. The profile supplies your
   controls; it is independent of the online player number.
2. Enable the mods you want before hosting. Open **DKR-R Online → Host settings**
   and choose the backend, menu ownership, maximum racers and input settings.
   These rules are frozen when the lobby is created.
3. In **Host / Quick Join**, name the lobby, choose the Online Adventure save
   source and select **CREATE LOBBY**.
4. Share the five-character code, or invite authenticated online friends.
   Guests use **REQUEST TO JOIN** or accept the invitation.
5. Review/approve pending guests. If mods are required, the client must consent
   and finish verified preparation before admission completes.
6. Everyone selects **READY TO RACE**. The host starts the synchronised countdown.
7. DKR-R boots through the normal game flow. Initial-state and scene barriers
   release only when the connected racers are ready.

By default, the host guides shared menus until Player Select. Every occupied
online slot then receives the right native player, without asking clients to
remap their physical controller to another local port.

There is no joining a match after loading starts. If a racer leaves or an
unrecoverable connection/state error occurs, the match ends and surviving
copies return to the launcher/lobby with a reason. This deliberately avoids
continuing with a missing input owner.

## Choosing a backend

| Host setting | Behaviour |
|---|---|
| Auto | Default for fresh settings; selects Experimental Rollback. It is a preference, not a fourth network protocol or automatic mid-match fallback. |
| Experimental rollback | Predicts missing remote inputs within a bounded window, restores owned game checkpoints and replays when late actual input differs. |
| Host prediction (legacy Rollback) | The older host-authoritative prediction path. Published commits are not rewound; portable host state repairs replicas. |
| Strict input sync (Lockstep) | Waits for the required input set before advancing. No gameplay prediction/replay; latency and jitter can cause waits. |

Existing explicit saved backend selections remain respected. Change the backend
before creating the lobby; switching does not happen underneath a running match.

Experimental Rollback is genuine gameplay rollback, not the older mode renamed.
It owns replay-safe guest/native state and keeps renderer output, confirmed
audio and persistence outside speculative replay. It supports admitted US
v1.0/v1.1 payloads and synchronised session mods in the integrated builds.
Its platform gates and real-world qualification still matter:
[platform support](PLATFORMS.md). It remains experimental.

## Delay, prediction and stutters

Use **Automatic input delay** as a starting point, particularly across longer
distances. For Experimental Rollback the UI recommends a 10-frame prediction
window for distant players; the selectable window is 2–20 frames.
A two-frame window may be shorter than the connection can deliver.

A bigger window allows more prediction, but also increases correction/replay
work. It does not remove latency or make a struggling machine fast. Manual
delay is a next-lobby rule, not a live fix for an already frozen session.

“Waiting for Player Inputs” can mean the bounded prediction window is exhausted.
Loading/scene/finish barriers can also wait for peer readiness or confirmation.
Neither mode is allowed to advance indefinitely on guessed input or discard
actual controls just to keep a frame counter moving.

Compare offline performance with the same scene and graphics settings first.
Use the detailed network overlay to distinguish input waits, replay work,
loading and presentation drops. When reporting a problem, send host and client
logs from the same attempt, the backend, player count, settings and scene.
See [troubleshooting](TROUBLESHOOTING.md).

## The host is the Mod Master

The host's enabled legacy tracks, characters and Track Lab courses form an
immutable lobby manifest. Clients see the required set and consent before any
missing content is downloaded or activated for that session.

Matching installed offline mods are reused even when locally hidden or disabled,
provided their source hashes, prepared assets, behaviour contracts and required
revision match. Verified caches are also reusable. Only missing payloads need
transfer, and completed preparation can survive retries.

The host and client can see download/preparation progress. Downloading is only
one stage: source decoding, reconstruction against the client's own ROM and
verification must finish before readying. Declining or failing preparation does
not partially admit a racer or fall back to different behaviour.

Verified received content is retained as inactive offline library entries where
applicable. It does not copy the host's activation/visibility over your choices.
No ROM, save collection or user-supplied executable is distributed.
Only share patches/artwork you have permission to distribute.

After changing a mod set, close and recreate the lobby. For import formats,
reviewed adapters and limits, see [custom tracks and characters](LEGACY-MODS-BETA.md).

## Saves and Adventure

The host chooses to copy offline Adventure, start fresh or continue previous
online progress. Copying reads the host's save; it does not replace it.
Clients install the checksum-validated session EEPROM in separate online
storage, never into their personal offline Adventure.

The route remains held until all runtime/save owners retire. Confirmed rollback
progress is the only progress persisted; a disconnect cannot turn an online
save worker into an offline one. Online Controller Paks are also separate.
See [saves and recovery](SAVES.md).

Two-player Adventure keeps the original shared-hub arrangement: a single
viewport/visible racer in the hub, and shared ownership chosen by the game's
controller-ID table. Boss/fly-in topology can differ from an ordinary race.
Online identities remain stable across those transitions; a one-racer scene
does not mean the second authenticated player has disappeared.
Three/four-player Adventure is not offered.

## Local settings and presentation

Your input profile, renderer settings, texture packs and master volume are local.
Texture packs need not match the host's cosmetic selection.

In Experimental Rollback, local scenery retention/view distance currently apply
to restored eligible billboards, not every actor/mesh. FOV, vehicle LOD and
other simulation-affecting camera/detail values remain canonical.
The offline Modern camera is disabled online.
Individual sound-category mixing is deferred; master volume stays local.
Saved preferences remain available offline and in the legacy modes.
See [Graphics](GRAPHICS.md) and [Sound](SOUND.md).

## Friends and privacy

**INVITE FRIENDS** offers authenticated online, unblocked friends. The receiver
gets a notification and an invitation under **OPEN LOBBIES**.
Invitations expire after five minutes and use a single-use authenticated
capability; normal code entry still requires host approval.

**APPEAR OFFLINE** hides advertised presence while preserving the secure route
and manual Quick Join. **ALLOW FRIEND LOBBY INVITES** can be disabled separately.
Neither deletes the friend list or profile.

Treat lobby codes as private invitations. They expire when the lobby closes
and no later than six hours after creation. Rekey/revoke controls invalidate
admission capabilities without giving strangers a player slot.

Use **Hide code** beside the lobby code when streaming. **Show code** reveals it
again; Copy still copies the real invite. Hiding it is not an access-control
change, so keep normal host approval enabled for people you don't know.

## Connection and compatibility

Quick Join uses the public PeerJS rendezvous for signalling, then authenticated
WebRTC peer channels for gameplay. It has no bundled gameplay relay. Restrictive
NATs, firewalls or mobile networks can prevent direct routing, and peers' public
addresses may be visible to one another.

Ephemeral X25519 agreement and per-peer XChaCha20-Poly1305 protect the native
session protocol; WebRTC adds DTLS transport protection. Invalid, replayed and
unapproved traffic is rejected. Reliable lifecycle/content traffic is separated
from latency-sensitive input scheduling.

Admission checks build/protocol, ROM, Patch Pipeline identity, gameplay/Magic
Codes, mod contracts, save proof, architecture and floating-point policy.
An ARM64 port does not automatically permit x64/ARM64 mixed sessions.
Use compatible peers; don't bypass a failed compatibility check.

Startup failures name the mismatched part: replay engine, Game Pak, prepared
mods, checkpoint schema/size, launch rules, online save or startup epoch.
The runtime log records local and remote fingerprints, not save contents.
For a report, send both logs from the same attempt plus the package names,
ROM revision and enabled mod set. Keep personal paths/invite details private.

There is no voice/text chat, spectator mode or general public matchmaking
directory. “Open Lobbies” is not a promise of an unrestricted public browser.
