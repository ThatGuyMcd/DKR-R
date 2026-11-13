# Lobby and in-game chat test

Chat now sits beside the player cards in the lobby. On a smaller window it moves
underneath them so the cards and controls still fit. Racers and spectators share
the same conversation. The existing player cards, pictures, friend controls and
race buttons are kept.

During a game, recent messages appear in a small feed. Press T to open chat,
type a message and press Enter to send it. Escape closes it. On a controller,
open the main overlay, go to Online, then Overlays and choose Open lobby chat.
The keyboard button opens the existing on-screen keyboard. B closes chat once
the keyboard is closed. The race keeps running while you type, with your local
driving controls held neutral.

Online > Overlays lets you change the chat key and feed position, or hide recent
messages during races. The options button in chat lets you mute individual
players on your device. The host can also turn lobby chat off for everyone.
Mute choices last for the current lobby. Chat history is kept in memory for
that session and clears when you leave or move to another lobby.

Messages use the current authenticated session connection, including its
selected proxy route. No website or API deployment is needed. The host checks
the sender against the admitted roster and supplies the display name. Chat
does not enter the rollback state, change the simulation or pause a race.

Messages are limited to 256 UTF-8 bytes, and history to 100 messages. The sender
sees a pending message until the host confirms it. Lost messages retry five
times after the first attempt. A message that cannot be confirmed is marked
Not delivered and can be retried manually without creating another copy.
Rate limits, bounded queues and control-channel backpressure keep chat traffic
below essential session traffic. New arrivals receive messages sent after they
join; earlier history is not replayed to them.

The module lives in runtime-recomp/src/game/netplay/session_chat. The existing
baselined Patch Pipeline connects it to the current session owner, launcher,
overlay, input polling and build. It does not modify dependencies or generated
guest code. These isolated test builds use the existing compatibility ledger
with fresh, matching Windows and Linux identities. Both-owner review remains
required before merging the compatibility change.

Automated coverage includes UTF-8 and packet validation, loss and duplication,
spoofed senders, spectator messages, bounded retries and history, live host
policy changes, input capture and room teardown. The native DirectSession
fixture also sends chat between three authenticated owners over the real
session transport. Package evidence records the checks actually run.

For acceptance, use the matching builds on every machine. Test host, client
and spectator messages in the lobby and during a race, keyboard and controller
entry, local mute, disabling chat, loss/reconnect, and leaving then making a
new lobby. Check the existing profile pictures, Add friend, joining progress,
live routes, Dynamic Input Delay and stock/custom Track Select at the same time.
Automated transport and startup checks do not replace that live race test.
