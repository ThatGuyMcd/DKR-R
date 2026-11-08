# v1.0.14 beta 1 lobby changes

The host measured party clients, but the party lobby projection discarded the
shared player ping fields. Local cards always replaced their ping with This PC.
Clients could therefore see a host reading of zero and no useful local reading.

The existing authenticated route snapshot now includes bounded host-relative
RTTs for admitted racers and spectators. Direct measurements expire after three
seconds without a successful probe; selected proxy measurements use the existing
fresh route sample. The complete client snapshot expires after four seconds.
Generation checks prevent attaching a previous roster's measurement to a card.
Unknown measurements display Measuring; the host is the reference, not a network
connection to itself. This is display metadata and never creates a transport
permission, supplies gameplay input or changes Dynamic Input Delay.

The existing connection check and invite actions are right aligned above the
ready panel. Narrow layouts stack the buttons at the right edge. Their existing
callbacks, permissions and disabled states are retained.

This is an isolated Windows x64 and Linux x86_64 AppImage beta. It retains the
previous online fixes. Both-owner compatibility review is required before merge.
No website or API deployment is required. The broader GitHub documentation update
will be planned after packaging, for approval.
