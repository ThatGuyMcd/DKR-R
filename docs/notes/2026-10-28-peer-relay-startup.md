# Proxy test build startup fix

The 27 October proxy test build could close immediately when opening DKR-R.exe.
It failed before the runtime log was configured, which explains why a new log
did not appear for those attempts.

Windows recorded exception `0xc0000005` in DKR-R-Runtime.exe at offset
`0x3ac5348`. The matching executable and PDB resolve that address to the proxy
transport constructor inserting into its registry map.

The game owns a DirectSession as a global object in runtime_netplay.cpp. Its
constructor now creates a proxy transport. That can happen before the separate
transport source file has constructed its global registry map. The earlier
networking tests created their sessions inside main(), after those initializers
had run, so they did not cover this ordering.

The registry now initializes on first use and stays available through global
session destruction. Transport replacement still checks the current owner before
removing an entry. The packaged close test also caught the lobby-status registry
being destroyed before that global session. That registry now has the same safe
lifetime, including its weak mod-sync bindings. No relay protocol, route
selection, Dynamic Input Delay,
launcher layout or existing gameplay fix changed.

The new startup regression deliberately constructs a transport before ordinary
global initializers. It reproduces the original Windows access violation with
the previous code, then passes with the fix. It also covers replacing a
transport under the same owner and shutting down after main() returns.
The native relay test also owns an application-global DirectSession, so its
subprocess has to complete the real session destructor before it can pass.

The replacement Windows ZIP and Linux AppImage use matching new test identities.
Everyone in the lobby needs these replacement builds. No API update is needed.
The sealed release and previous test archives remain available unchanged.

Validation passed: all 145 host tests, native startup and relay tests, retained
regressions, source/package checks and archive readback. The packaged Windows
launcher presented its first frame using a separate test profile, then closed
normally with exit code 0. The packaged Linux AppImage completed its headless
settings self-test and exited with code 0. Those smoke-test results are included
with the replacement downloads. Neither smoke test is a live multiplayer race.
