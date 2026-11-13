# Live connection routes on lobby cards

Each occupied racer card now shows Direct or Via followed by the other player's
name. The host's card says Host. Proxy routes use the existing green text colour.
Long names shorten to fit, and hovering shows the full route. The cards keep
their current layout, profile pictures, friend buttons and readiness controls.

The host shares a small authenticated status snapshot with every admitted
member, including spectators. Route changes are checked every quarter second
and a fresh snapshot is sent at least once a second. This also works when proxy
connections are off. A client uses its own confirmed route for its own card.
Other players' cards say Checking route if their host status is over four
seconds old. Closing or rehosting clears the previous status.

These snapshots carry member identities, names and the selected proxy identity.
They contain no addresses, relay keys or gameplay data. Only the authenticated
host can publish them. Sequence checks reject older updates, and malformed
updates leave the last valid snapshot intact. They cannot grant a connection
route or change input delay, game state or rollback.

The existing Patch Pipeline integration includes the drawing code. Route status
and its tests live in the normal first-party peer_relay module. The maximum
twelve-member lobby fits inside the existing control-message limit. No website
or API deployment is required.
