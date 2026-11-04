# Mod Store in the overlay

The Mod Store now appears under Mods/Hacks while a game is running. It uses the
same library, account and discussion as the launcher. The existing tabs and
launcher design stay in place.

Mods/Hacks and the Mod Store also explain that some legacy .xdelta mods need a
clean US v1.0 ROM to install. Add it in Play once; v1.1 can stay selected for
playing. The notice appears in the launcher and overlay without duplicating it
above the Store. Import checks and ROM handling stay as they are.

Downloads are queued until the player returns to the launcher. Installation,
removal and changes to active mods remain locked during gameplay. Cover images
use the active in-game graphics backend; SDL launcher textures are never passed
to it. The image cache has 32 entries, uploads at most one small image per UI
frame and only evicts an image after the existing presentation fence completed
its last draw. Vulkan images use their own bounded descriptor pool. No extra
gameplay GPU fence or drain is added.

Download and comment counts come from the website's existing community API.
Comments have their own tab alongside the existing star reviews. Players can
read anonymously, or use their Discord account to post, edit, delete or report.
Comment pages hold at most 25 entries. Account changes discard old ownership
data; late responses cannot replace another mod's discussion. Drafts and retry
identifiers survive switching discussions within the same signed-in account.

Launcher file requests also contribute to the shared download counter. The
attempt identifier survives a retry or resume. Verified cached files do not
make a new request. Counting uses a separate, bounded background queue, pauses
during gameplay, and cannot delay an account action or stop a verified file
download. It never follows the API redirect or forwards credentials to a file
host. All existing size, digest, package and redirect checks remain in place.

The website community Worker and its additive database migration must already
be deployed for counts and comments. An older API shows an unavailable message;
it still supports the existing library and downloads. No website, FTP or API
source needs changing for this launcher feature.

Verification includes the production discussion implementation with isolated
HTTP fixtures, the existing Worker/D1 community suite, required host tests and
the existing Patch Pipeline source/binary audit. A private empty-profile test
opens the actual overlay and exercises comments, counts, native cover uploads
and cache turnover while the owned online race continues. Normal launches
cannot arm it through environment variables alone.
