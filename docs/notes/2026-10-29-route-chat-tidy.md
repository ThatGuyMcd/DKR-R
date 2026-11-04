# Client route labels and chat layout

Classic LobbyState includes player slots and names, but deliberately omits peer
IDs. The host has those IDs locally; clients do not. Passing an empty ID to the
route label left their cards on Checking Route even after connection succeeded.

The authenticated route heartbeat now includes an optional slot map and lobby
generation. Client cards use that map when their player ID is absent. Their own
card uses the local endpoint's current selection immediately. Other cards wait
for a matching generation; duplicate names never identify a player. Expired
heartbeats still expire the labels. Slot metadata does not install routes or
change transport authority. The parser retains support for the earlier heartbeat
format, and rejects duplicate, out-of-range or incomplete slot maps atomically.
The card's picture and friend controls use the same resolved identity. This also
fixes missing pictures/actions for classic clients whose original slot has no ID.
Only the four UI card copies are projected; authoritative session state is kept.

The chat field uses a hidden ImGui identifier. Its existing keyboard button now
sits directly beside the field without the extra message label. The in-game feed
does not create a window while idle. Up to three recent, unmuted messages appear
for eight seconds, with a small key hint instead of a full-width button. Pressing
the chat key opens the editor, including when passive messages are disabled.
The passive feed cannot take mouse or controller focus. Keyboard, mute, delivery,
host permissions and gameplay input capture remain unchanged.

Avatar drawing also had two separate problems: initials were always drawn below
the image, and a miss in the 32-image download cache hid a still-valid entry in
the 48-texture UI cache. Ready pictures now replace initials entirely, with a
neutral backing for transparency. A download-cache eviction keeps the existing
texture visible. Failed replacements keep the last good texture; replacement
and eviction still cannot retire a texture already referenced by this frame.
Renderer changes clear the old texture owner. A fixture executes the production
draw routine through cache misses, dimmed/transparent images, failed uploads,
same-frame replacement, capacity pressure and renderer retirement.

Regression coverage includes the real DirectSession classic client views with
empty IDs, duplicate display names, proxy selection and fallback, roster changes,
stale snapshots, malformed mappings, feed expiry and mute filtering. Matching
Windows and Linux AppImage tests retain the earlier online performance fixes.
Use the existing compatibility ledger for these isolated tests; both-owner
review remains required before merging. Real remote gameplay remains a playtest.
