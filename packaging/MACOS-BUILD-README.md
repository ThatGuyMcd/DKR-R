# macOS builds and release coverage

Published v1.0.8 includes a notarised macOS ARM64 ZIP and an iOS ARM64 IPA.
macOS and iOS are skipped in the v1.0.12 Windows/Linux release set.
Do not rename an earlier Apple asset to v1.0.12 or include it in that manifest.

Use [Building DKR-R](../docs/BUILDING.md#macos) for Xcode/Homebrew preparation,
source builds, ad-hoc packaging and Developer ID notarisation. The source kit
contains the dependency lock; first preparation fetches the pinned checkouts.
Preparation checks staged Python/native tools and repairs incomplete stages
without deleting protected source.

The current integrated Rollback backend remains gated out on Apple. The
v1.0.12 public Rollback-only layer does not promise a legacy-mode fallback.
Current feature integration, bundle/updater ownership, signatures/notarisation
and physical-device acceptance need qualification on an Apple host before a
new Apple release. Source compile checks are not release packages.
See [platform support](../docs/PLATFORMS.md) and [release validation](../RELEASE-VALIDATION.md).
