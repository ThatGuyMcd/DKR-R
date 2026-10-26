#pragma once
// Online identity: the owned rollback contracts peers compare before they
// admit each other, frozen at the v1.0.11 values. docs/contracts/ONLINE_IDENTITY.md
// records them and where they came from. They change only by a deliberate
// edit here, never because a source file changed, and a change makes this
// build refuse every v1.0.11 peer. cmake/OnlineIdentity.cmake reads them into
// the DKR_OWNED_BUILD_ID_V77/V80 compile definitions.
#define DKR_LEGACY_OWNED_BUILD_ID_V77 "5e22da8e6095b362ede17c69a2341aae2df24cbf8561c3af57fc86566d11784e"
#define DKR_LEGACY_OWNED_BUILD_ID_V80 "32c82fea3111440434926d5782e174c9f2d036d198f039c5424393a835f948b0"
