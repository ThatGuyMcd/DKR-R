# Public lobbies and online presence

In v1.0.12, use **DKR-R Online** to connect, browse **Public / Friends** and host
a **Private / Public** lobby. These are additions to the existing launcher
layout. No website frontend or new standalone account is required.

## Sign in once

Choose **Sign in with Discord** and complete the normal browser pairing flow.
An existing Mod Store sign-in is shared with Online. Signing out or switching
accounts updates both uses. Local play and achievements do not need sign-in.

Enable **Keep me signed in** before signing in. Windows uses DPAPI. Linux uses
the desktop keyring where available. Without one, including Steam Deck Gaming
Mode, the launcher offers device storage in your profile with owner-only file
permissions. It contains a remembered sign-in credential, so keep that profile
private. Turning remembering off removes the saved sign-in. After upgrading a
session-only installation, sign out and sign in once to save this device.

## Public and private rooms

**Public** shows advertised public rooms from the shared worldwide service.
Use **Request to join** for a racing place or **Spectate** where available.
The existing friend/code join controls also offer Racer / Watch Race. Available
places are checked again by the service and native host; selecting a card is
not a reserved controller port or permission to skip approval.

Hosting defaults to **Private**. Private rooms are absent from the public list
and use the existing friend/invitation or lobby-code route. Valid friend invite
capabilities retain their normal admission behaviour. Ordinary code requests
still need host approval. Hide the displayed code while streaming.

**Appear offline** hides friend presence and changes public hosting to private.
It is not the aggregate-count disconnect control. **Disconnect Online** removes
this launcher instance from service presence. Blocking, declining, locking a
room or changing visibility must not be bypassed by a cached public listing.

An existing private lobby created while the service was unavailable remains
private under its original native workflow. Connect first, then recreate that
room to publish it. Existing peers do not acquire new account identities silently.

## What the online total means

The total counts connected authenticated accounts, including private rooms
without listing them. Several instances of one account count once, with a
maximum of five connected instances per account. It does not count every
offline player, connected controller or spectator separately from their account.

Heartbeats run about every 20 seconds. A clean disconnect removes presence;
a crashed or sleeping launcher can remain visible for up to the 60-second
lease. Counts/listings can therefore lag briefly. Reconnect rather than making
duplicate rooms if your connection drops.

## Discovery and gameplay have separate jobs

The existing authenticated library API supplies account authority to one
worldwide directory. The directory keeps presence, public summaries, room
visibility and short admission reservations. Public summaries exclude reusable
codes, tokens, account identifiers and transport addresses.

The native host still checks role/capacity, approval, exact content, saves,
Ready and start. The website never runs Rollback or assigns inputs. The current
PeerJS/STUN and authenticated peer transport carry the game after admission.
An outage can stop discovery/new joins while existing admitted peers continue.
Direct connection limits still apply; there is no bundled gameplay relay.

Approved preparation can outlast a directory reservation without restarting a
long mod transfer. Real authorization changes, including locking, privacy or
starting, can revoke unused/obsolete admission. Benign account refresh and
roster notifications do not revoke approved work by themselves.

## Service configuration and operations

`ONLINE_ENABLED` must be true in both the main API and private Online Worker
for discovery. `RUNNING_SPECTATORS_ENABLED` is a separate switch in both; leave
it false until late-spectator checkpoint/catch-up acceptance passes. Waiting
spectators and late racers have different admission rules.

The service uses a private binding to the single named SQLite Durable Object
`dkrr-worldwide-v1`. It has no public private-worker route. Deploy the private
Worker first and preserve the live API's existing account/FTP/database bindings.
The launcher and existing website API are the feature's integration points;
FTP remains the mod-upload method.

Profile pictures and Discord metadata also need the private API's profile D1
migrations and matching Workers. An already deployed friends/pictures bundle
does not need redeploying for the final header layout. See [profiles and deployment](ONLINE_PROFILES.md#deploying-the-matching-profile-api).

See the [deployment and rollback instructions](../services/online-directory/DEPLOYMENT.md),
[local backend QA record](../services/online-directory/QA.md) and
[owner acceptance checklist](../services/online-directory/PLAYTEST.txt).
Do not treat local synthetic load tests as proof of worldwide network behaviour.
