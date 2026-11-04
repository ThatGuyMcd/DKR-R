# Lobby profile refresh

Hosts could see joining players' pictures and Add Friend buttons while those
players could not see the host's. This was reported in both public and private
v1.0.12 lobbies.

The native client does publish its joined code and full sender identity. The
local API fixture also permits a verified client to read host and other-client
profiles. Neither path needs broader access or a change to race transport.

The profile publisher had two lifecycle faults:

- It stopped publishing whenever a native session was active. Joining or hosting
  before optional profile registration finished could leave an old or missing
  server profile in place. The same restriction prevented renewal in long lobbies.
- It waited 12 hours after starting an attempt, including failed attempts. The
  receiving launcher rejects expired friend capabilities, which removes the
  associated picture and friend action together.

Publication now runs during an active session on the existing account worker.
Only a matching successful response starts the 12-hour renewal interval. Failed
attempts back off from one to 32 minutes. A changed launcher key or renewed friend
capability starts a fresh publication. Busy account requests and unsaved picture
edits still defer it.

Account picture metadata also loads before this launcher has a registered social
link. This preserves the account's existing sharing preference during linking.
It does not automatically enable picture sharing or import a Discord picture
without the existing choice.

The native client bridge fixture covers a 20-digit sender identity. Profile
scheduler checks cover failures, unrelated responses, account/key changes and
capability renewal. The local Worker/D1/SQLite fixture covers a joined client
receiving host and other-client pictures and friend capabilities after a session
refresh, plus picture opt-out and membership revocation.

No Worker, database, FTP, rollback, admission protocol or launcher layout changes
are required. Automated fixtures do not establish what was stored in a reported
player's live account; the two-machine check below remains the final confirmation.

For testing, use the matching Windows and Linux builds. Restart and join quickly
after Discord sign-in restores, then check a public lobby and a private code join.
Use accounts that are not already friends. The client should see and be able to
request the host and another client as friends. Acceptance still belongs to the
recipient. Turn picture sharing off and check that initials replace the picture
while Add Friend still works. Leaving or closing the lobby must retire the old
player actions. These test packages do not replace the sealed release or its
signed update files.
