DKR-R v1.0.7 beta 7 - DEV MENU STARTUP FIX

Use the application in this package together with Dev Menu 2.0.1. The corrected
mod is included under Optional-Mods and in the SDK. Import and enable it through
MODS / HACKS, then start a game. Open Tools in the overlay or press F2.
The previous beta 7 app cannot run development mods past its save-route check;
re-importing the mod alone does not repair that second issue.

The log showed:
  [save][scope] refused mutable route switch
  [legacy][save] Refusing guest entry: isolated save route did not activate.

This was an app startup bug, not a new graphics crash. The launcher selected an
isolated development save, but guest entry then requested the old mod-set save.
The runtime correctly refused that change and stopped before mod boot ran.
Startup now verifies and retains the already loaded, leased save. A mismatch
still fails closed and reports an error through the launcher's recovery flow.
Ordinary saves, mod-set saves and development saves stay separate. No existing
save is moved, deleted, overwritten or migrated by this delivery operation.

This also includes the earlier Dev Menu import correction and updated SDK
builder. Runtime guards and all protected dependency/generated sources remain
unchanged. The SDK's public APIs and existing mod ABI are unchanged.

Everyone in an online session must use this same Startup Fix app and the same
enabled mod editions. Its native build identity differs from the first beta 7.
macOS remains excluded. Linux and Steam Deck include AppImage packages.

Compilation, source and static package inspection only. No game/mod, replay,
unit-test, multiplayer or headset execution was performed by the agent.
Gameplay, controller, visual and performance acceptance remain with the user.
