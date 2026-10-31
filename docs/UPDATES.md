# DKR-R updates

## Updating to v1.0.12

Use **Check for updates** in Play or About DKR-R. Choose **Download update**,
then **Install update and restart** after playing. Startup checks can be turned
off, a version can be skipped, and beta/playtest releases are optional. Stable
releases are the default. A failed check does not stop offline play.

v1.0.12 has Windows x64, Linux x86_64 and Linux ARM64 update packages. Steam
Deck uses the Linux x86_64 package. Android, macOS and iOS are excluded from
this release. Android releases are no longer supported. Older installations
on excluded platforms must not be pointed at a different platform's download.

Finish imports and leave Online before installing. Downloads stop when the
game starts. Partial downloads can resume if the server supports it, and
verified completed downloads can be reused. v1.0.6 needs a manual upgrade to
an updater-enabled version. There is no need to disable signature checks.

## Keeping your data

Updates replace application files. Your ROMs, saves, mods, textures, controller
settings and logs stay in your profile. Portable mode keeps portable.txt and
dkr-runtime-data. Restart to apply a pending portable-mode change before updating.

On Windows, start DKR-R.exe at the top of the complete extracted folder. Its
runtime, DLLs and helpers live in libexec/dkr-r. Keep assets and libexec with
the launcher. Do not copy just one executable out of the ZIP. For a manual
upgrade into a new portable folder, copy both portable.txt and your existing
dkr-runtime-data before launching. Normal profile installations reuse the profile.

Linux and Steam Deck replace the whole AppImage at its existing path. Make it
executable when installing manually. A read-only folder can keep playing but
cannot accept an in-place update. Developer/package-manager installs update
through their existing manual/package route.

Windows transaction backups are under .dkr-update-backups; AppImage backups
sit beside the old application. The profile's updates folder holds download
and handoff records. Keep these while investigating a failed installation.
DKR-R.exe checks interrupted Windows updates before loading the runtime.
The Updates section reports a failed or recovered previous installation.

For interrupted AppImage/bundle swaps, close all instances and use the copied
helper from the recorded updates/handoff-… folder:

```text
DKR-R-Updater --recover <that-folder/request.json>
```

Recovery checks the signed manifest and the exact recorded backup. It does
not search other folders or delete user data. Do not remove the only backup.
Older Android/Apple builds retain their own installer and distribution rules;
there is no new APK, app bundle or IPA in the v1.0.12 set.

## Preparing the release update files

The exact filenames are **DKR-R-update.json** and **DKR-R-update.sig**. Upload
both beside the final downloads. The stable tag is **Version1.0.12**, and must
match the manifest. The source commit must contain all accepted Patch Pipeline
layers and the final release metadata.

The manifest records each package's version, size, SHA256, platform and minimum
OS requirements. Windows also has a complete file table and the package's
assets/updates/install.json ownership receipt. Steam Deck uses linux-x86_64;
it does not need a separate manifest entry. Linux requires glibc 2.38, with
GLIBCXX_3.4.32 and a compatible Vulkan driver also needed by these packages.

```text
python -B scripts/release/update_assets.py sign --packages <finished-release-folder> --commit <full-source-commit> --private-key <protected-original-key.pem> --output <fresh-signing-folder>
python -B scripts/release/update_assets.py verify --packages <finished-release-folder> --output <signed-pair-folder>
```

The wrapper uses the existing scripts/update_release.py signer and the pinned
launcher public key. It verifies package receipts, Windows ownership and the
exact Steam Deck copy. Its signature is 128 hexadecimal characters encoding
the 64-byte Ed25519 signature over the exact JSON bytes, including the newline.
The verification receipt records the signed files and package hashes.

An old signature cannot sign a changed manifest. Keep the original private key
outside the repository and release folders, with a secure backup. Never send
it in chat or upload it. A deliberate key rotation needs a bridge release
signed by the old key; replacing the pinned public key would strand old clients.

All package changes happen before signing. Do not edit JSON, rename packages
or recompress archives afterwards. Verify the downloaded GitHub assets too.
Prepare the release as a draft, upload the four downloads and both signed files,
check everything, then publish. Use a new version for later fixes instead of
replacing already published assets. Publishing makes the update discoverable.

## Checks before rollout

Run the updater signing/tooling and native updater tests. Check a v1.0.11 notice,
download, install and restart on actual Windows/Linux installations, including
portable data and Unicode paths. Keep interrupted-download/install recovery,
corrupt signatures/packages, locked files, multiple instances, read-only folders
and disk-space failures in the checklist. Automated checks and final device
acceptance are recorded separately in [release validation](../RELEASE-VALIDATION.md).

See the [v1.0.11 signing notes](releases/1.0.11/UPDATER.txt).
