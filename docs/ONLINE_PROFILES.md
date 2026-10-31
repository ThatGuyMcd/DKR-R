# Online profiles, pictures and remembered sign-in

Online and the Mod Store use the same Discord account and display name.
Open **DKR-R Online → Online Profile**, edit the name and choose **Save profile**.
The same name appears to friends and in the Mod Store. An active lobby keeps
its current name until you leave. A failed save keeps your draft for retrying.

## Pictures and friends

**Change profile picture** lets you preview a PNG or JPG, use your Discord image
or return to initials. **Save Picture** applies the selected picture and sharing
choice. Sharing is optional. Other players only receive the selected picture
when sharing is enabled; the original file and its metadata are not uploaded.

Discord sign-in offers separate choices for importing its display name and
image. Sign out and back in once if the account predates the profile API update.
The Friends tab uses compact cards, with **Add a friend** above Search. You can
also send requests from eligible lobby player cards. The other player still
needs to accept the request.

## Staying signed in on Steam Deck

Enable **Keep me signed in** before signing in. Linux uses a desktop keyring
where available. If there is no keyring, the launcher can keep the device
credential in your profile using owner-only file permissions. This works in
Gaming Mode and Desktop Mode when they use the same application profile.
Protect that profile like a remembered login. Turning remembering off removes
the saved credential. Windows continues to use DPAPI.

After upgrading from session-only sign-in, sign out and sign in once to save
the device. A different Linux user or portable profile has separate storage.

## Deploying the matching profile API

If you have already deployed the friends/profile-picture API bundle, the final
profile-header layout needs no extra deployment. A fresh server needs both the
existing directory setup and the profile migrations. The website's FTP files
and mod-upload method stay as they are.

Build the owner bundle from the maintained service modules with the pinned esbuild:

```text
python -B services/online-directory/build.py --output <fresh-bundle-folder> --esbuild <esbuild-executable>
python -B services/online-directory/build.py --output <bundle-folder> --verify
```

1. Back up the live private API Worker, directory Worker and private API D1
   database. Keep the bindings, secrets, routes and feature settings too.
2. Run the bundle's `PRIVATE-API/001-launcher-discord-profiles.sql`, followed by
   `002-launcher-profiles.sql`, against that existing D1 database. Both are
   idempotent. They are not migrations for the directory's Durable Object.
3. Update the existing directory Worker with `ONLINE-WORKER/directory.mjs`.
   Keep its bindings, Durable Object migration and current `ONLINE_ENABLED`
   and `RUNNING_SPECTATORS_ENABLED` settings.
4. Update the existing private API Worker with
   `PRIVATE-API/worker-single-file.mjs`. Keep all current variables, secrets and
   bindings. No new service binding is needed.
5. Use matching v1.0.12 launchers. Sign out and back in once, then check the
   separate Discord name/image choices, custom picture preview and sharing.

The API stores one small selected avatar per account and verified links to
launcher identities. Without this API update, existing online play remains
available and the profile explains why image import is unavailable. The
gameplay transport and Rollback path are unchanged.

For rollback, restore your saved Worker code and configuration. The added
tables can remain. Prefer your live backups over an older bundled rollback
copy if you have made other API changes. See the [original directory setup](../services/online-directory/DEPLOYMENT.md)
for a new installation and the generated bundle's instructions for its exact files.
