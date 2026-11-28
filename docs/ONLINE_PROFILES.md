# Online profiles, pictures and remembered sign-in

Online and the Mod Library use the same Discord account and display name.
Open **DKR-R Online → Online Profile**, edit the name and choose **Save profile**.
The same name appears to friends and in the Mod Library. An active lobby keeps
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
