# Launcher and overlay

Enable **Skip launcher** beneath **Portable mode** in Play to start the selected
ROM automatically on the next application launch. The usual enabled-mod and
save preparation still runs. Missing ROMs or preparation errors show the launcher
instead. Press Escape/B or **Open launcher** during preparation to cancel this
startup. Returning to the launcher after a game does not start it again.
Turn the setting off in **Play** in the in-game overlay to restore normal startup.
The preference is saved in the current profile, including portable profiles.

The launcher and in-game overlay use the same pages, spacing and card layouts.
Play keeps the same layout while a game is running, with **Return to the race**
in place of the offline Play button. ROM changes, mod installations and save
editing stay locked while playing. Installed Mods uses the full content width
in both modes, including Textures and its legacy ROM installation notice.

**Ready to race** is centred within its part of the Play card. The save summary
has a small three-light indicator: green for Vanilla, amber for Modded and blue
for Online. The label says **Using** for the running game and **Next** before
launch. Ordinary offline mods still use your shared personal Adventure; the
amber label says so. Online and isolated development progress use their own
saves, and the preview follows the running save owner.

Use **Sidebar navigation**, just above the divider and Restart button, to
choose **LB / RB** or **D-pad**. In D-pad mode, Up and Down change sidebar pages
and the left stick moves between elements in the page on the right. The right
stick scrolls that page. The stick mapping stays the same in LB / RB mode,
which also lets the D-pad navigate the page. The D-pad cannot move both the
sidebar and page at once. The choice is saved and works in the launcher and
in-game overlay. Scrolling targets the active dialog when one is open.

Pick your ROM and start from **Play**. Desktop portable mode selects a separate
data location on the next launch; it does not move your existing profile.
The **Language** dropdown beneath Portable mode changes the launcher and
in-game overlay immediately. **Automatic** follows the system's preferred UI
language, with English fallback. The choice is stored with the current profile;
portable and installed profiles keep their own choice. It does not translate
the ROM, mods, player chat, names or downloaded descriptions, and does not
change the computer's region or seasonal calendar. The initial translations
are test drafts. See [Localisation](LOCALISATION.md) for coverage, contribution
and review guidance.
**Skip intro** can start at the Start Menu or Character Select. The Start Menu
route keeps its normal first sequence and music. **Check for updates** is a
small manual action in Play; background release checks still need your approval
before downloading/installing an update.

**DKR-R Online** keeps Host / Quick Join, friends/profile and host settings.
The existing Open lobbies panel now offers **Public / Friends**, the header
shows the distinct connected-account total, and the profile reuses **Sign in
with Discord**. Hosting adds **Private / Public**, with Private the default.
The lobby retains approval, Ready, start, spectator/role and leave controls.
**Check Rollback connection** opens the shared Scoreboard. The host mode is
Rollback; the old backend selector is removed. Hide the lobby code when streaming.
See [Online](ONLINE_MULTIPLAYER.md), [Public lobbies](PUBLIC_LOBBIES.md) and
[Spectators](SPECTATORS.md).

Online Profile now has one shared name and picture header, Privacy and friend
code cards below it, and a compact Account settings footer. Friends use smaller
cards, with Add a friend above Search. See the [profile and friends guide](ONLINE_MULTIPLAYER.md#profile-pictures-and-friends).

**Mod Library** has its own sidebar button above **Installed Mods**, in the
launcher and the in-game overlay. Browse free mods, see download counts and
read or post comments using your existing account. Downloads started in-game
queue for the launcher so installed mods stay fixed while playing.

**Installed Mods** has tracks, characters, Vehicles, Custom Magic Codes,
Track Lab and import/help tools. Cars can be imported as `.dkrkart` or ZIPs and
chosen in the native Track Select picker. Character cards now show portraits like vehicle cards. Cards stay compact; Details contains
longer help and settings. See [Mods](LEGACY-MODS-BETA.md), [Vehicles](VEHICLES.md)
and [Magic Codes](MAGIC_CODES.md).

All four My mods categories use the same Search, Status and Sort layout.
Vehicles supports On/Off filtering and name sorting too. Filters, duplicate
review and Clear filters sit beside the result count, below the search row.
The Tracks format key is inside Filters. Smaller windows stack the controls
without squeezing their keyboard buttons out of view.

Mod Library textboxes put their on-screen keyboard button on the right,
including comments, reviews and submission forms. Labels stay above the input.
Longer textboxes keep the button at their top right. A very narrow field puts
the keyboard below the input. Typed text uses the regular reading font.

**Graphics** and **Sound** apply the same settings in launcher and overlay.
Use Escape, F1 or controller Back/View for the overlay. Optional water/reflection
quality and lava glow have separate controls. [Graphics](GRAPHICS.md) explains
their scope and cost. Camera++ is an optional mod, not a forced camera replacement.

**Controls** selects input mappings. Text entry offers an on-screen keyboard;
controller navigation and scrolling are available in supported editors.
Device, Button mapping, Stick response, Motion and Shortcuts use the same
lettered category tabs as My mods. The row wraps on smaller screens.

**Controller feedback** in Controls has Classic and Enhanced modes. It has
separate settings for each local player and controller type, left/right tests
and hardware output diagnostics. See [Controller feedback](CONTROLLER_FEEDBACK.md)
for the DualSense and LCD Steam Deck test steps.
The optional analogue driving mod keeps its bindings, live input bars and
calibration in **Custom Magic Codes > Analogue Acceleration & Braking > Details**.
Choose your local player there. Existing bindings and calibration are kept.
Android has a Menu drawer and editable touch controls rather than the desktop
SDL3 helper. Keep access to Menu when hiding racing controls.

**Play > Save Manager** manages personal progress, online progress, development saves,
Paks, transfers and recovery. Ordinary offline mods share your personal
Adventure. Online and development progress have separate save owners.
See [Saves](SAVES.md) before restoring progress.

**Installed Mods > Textures** imports/enables packs and sets priorities.
Texture Packs and Texture Dumping are always open. **Texture Dumping**
has Export, Capture and My Dumps tabs; switching
tabs does not cancel an active job. See [Textures](TEXTURE_PACKS.md).
CRT filters are in **Graphics > HUD & effects**.

**Play > Achievements** shows 94 local goals, filters, pinned goals,
notification settings and progress import/export. It needs no account.
See [Achievements](ACHIEVEMENTS.md).

The launcher picks its seasonal theme from your computer's region and local
date. It uses the original clouds with new rendered corner decorations.
There is no Launcher appearance section to set up. See
[Launcher themes](LAUNCHER_THEMES.md) for the supported calendars.

**About DKR-R** contains
updates and the support summary above the credits. Support actions can open or
export logs/crash information; review private paths and lobby details before
sharing. Extra Save Location/Connection Details/Reflection Details disclosures
have been removed. Detailed instructions live in the repository docs instead.
## Page layout and movement

Play keeps the ROM controls on the left and the offline launch options on the
right of the same card. Smaller windows stack them inside the card. The Play
button stays underneath. Play shows Community Spotlight above this card.
Play and Installed Mods also show the Mod Library's
featured mods, with artwork, creator credits and a link to the mod's details.

The spotlight changes every 30 seconds while it is visible. Hovering it or
focusing its controls pauses the timer. You can also pause it or move
back and forward yourself. A saved catalogue still works without a connection.

Pages and section changes fade in using the same launcher style. Vehicle
cards now have the same staggered entrance as tracks and characters. Mod
Library uses the red heading with its orange outline and the same section
buttons as Installed Mods.

Community Spotlight stays above the Mod Library tabs on every page, including
mod details. Its artwork follows the rounded corners. Library pages use the
available width. The Library grid fits up to six cards across. Titles use up to
two lines, with the full name on hover and in Details. Artwork and metadata stay
compact, without stretching the whole row for one long title.
Refresh and Website use the same orange signs as Check for updates.
The card grid is the only browse view; Browse and Inspect and its view switch
have been removed. Search, Keyboard and Sort by share one aligned control row.
Sort moves underneath on narrow screens. Cards still open full mod details,
comments and download options. Old saved view choices no longer change the grid.

Textures is now inside Installed Mods, between Magic Codes and Track Lab.
Texture Packs and Texture Dumping are always expanded, so their controls are
visible when you open Textures. Export and Capture still start only when asked.
Texture packs and dumping are still there; CRT controls stay in Graphics.
Enabled mod cards now say ON. Changes still apply on your next launch.

Achievements and Save Manager are orange buttons at the bottom of Play's ROM
area, on the left and right. Narrow windows stack them. They are also in the
in-game Play overlay. Both pages have a Back to Play link, and their existing
save and gameplay restrictions stay in place.

## Session mods

With Skip launcher enabled, automatic startup displays the circular Recompiled
logo over the scrolling launcher clouds. Starting Game and the compact Open
Launcher button use an intro-style translucent grey caption. When the effective
next-session selection includes mods, Loading Mods and the current preparation
stage appear there too. Counted batches show completed work; stages without a
known total use an indeterminate bar. The bar follows actual work; the presentation fade lasts 350 ms.

Open Launcher, Escape or controller B cancels the pending launch and returns to
the launcher. Errors return to the existing actionable error dialog. Manual Play fades over 350 ms into this same screen and shows preparation
inline too. The prepared result waits for the fade to finish. Skip launcher
remains editable in the in-game Play overlay. Initial online startup uses
**Syncing Game / Getting everyone ready...** with an indeterminate progress
bar, then fades away over the first ready game presentation. It does not
reappear during later scene transitions. Native renderer creation can still
cause a brief window transition before drawing is available.

The orange Session mods switch sits between Achievements and Save Manager in
Play, in the launcher and overlay. ON prepares your selected content. OFF starts
a vanilla session, bypassing installed tracks, characters, vehicles, Custom Magic
Codes, Track Lab courses and texture packs, including selected built-in Magic
Code effects. Individual enable states, priority and save files are preserved.
Turn it back ON to restore the selected setup.

The saved choice is frozen when preparation starts. A change while playing
applies to the next session, including Skip launcher and direct launch. It never
changes the current race. A new lobby hosted with OFF has vanilla content; an
existing lobby keeps its accepted manifest. Joining clients always follow the
host's required content. Local graphics enhancements and standalone look behind
remain graphics features. The Adventure indicator describes the actual running
save/session, or the next offline selection when no game is running.
