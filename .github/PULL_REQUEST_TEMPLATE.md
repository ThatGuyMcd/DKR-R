## What

<!-- One paragraph: what changes and why. -->

## Pattern checklist

- [ ] First-party changes are in-place edits under `runtime-recomp/`, `modding/` or `scripts/` (no new `patches/<name>/`)
- [ ] Third-party changes are `*.patch` files registered in `patches/manifest.json`
- [ ] Guest hooks only via `guest-hooks/hooks.toml`; symbols only via `symbols/*.toml`
- [ ] New settings, launcher pages and owned services are registered, and their contract docs are updated
- [ ] `netplay/legacy_identity.hpp` updated (reviewed by both owners) if simulation, wire or owned-service behaviour changed
- [ ] Tests added or updated; `python scripts/check.py --suite all` passes locally
- [ ] Platforms considered: Windows / Linux / macOS / iOS / Android (say which ones you built)

## Risk

<!-- What could break online play, saves or mods, and how to roll it back. -->
