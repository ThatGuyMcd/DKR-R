# v1.0.8 release build record

Windows x64, Linux x86_64/ARM64 and Android ARM64 compile/link and static package
inspection completed. Steam Deck uses the exact Linux x86_64 AppImage bytes.
All four native builds share both revision-specific owned online identities.
Android retains the existing certificate/application ID, with versionCode 1080001.
Linux ABI requirements remain GLIBC 2.38 and GLIBCXX_3.4.32.

The compiled Windows runtime reserves 4 GiB but commits only the original
512 MiB usable RDRAM. The untouched POSIX/runtime remainder and staged source
selection were inspected on every build. No guest size, save, shader, SDK or
accepted water-body change. Checked-out dependencies/generated guest code stay
read-only; the normal Patch Pipeline composes the dependency fix in build copies.

No game, launcher, mod, unit suite, replay or benchmark was executed. Owner
confirmation on the affected PC and device gameplay acceptance remain pending.
The old logs identify the allocation path, not the precise Windows error/RAM
state. The new diagnostic retains the error number for a genuine future failure.
Manual downloads are complete. Automatic-update signatures still need the
existing private release signing key; it was not replaced. No push/tag/publish
was performed as an implicit part of this build pass.

Private evidence: D:/DiddykongWorkFolder/release-v1.0.8-20261020/evidence.
The preceding stable build record (`patches/startup-memory/history/RELEASE-VALIDATION-1.0.7.txt`
before the fold, in git history) is preserved as history, not reclassified as new gameplay acceptance.
