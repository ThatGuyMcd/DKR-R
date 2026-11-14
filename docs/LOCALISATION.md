# Launcher and overlay localisation

Play has a **Language** dropdown immediately beneath **Portable mode**. It also
works while playing. Switching language updates application copy without
restarting the game, rebuilding the font atlas or recreating either UI context.
Searches, form drafts, focus, controller bindings, mod selections and the online
session remain with their existing owners.

The initial group is English (`en`), French (`fr`), German (`de`), Spanish (`es`),
Italian (`it`), Brazilian Portuguese (`pt-BR`), European Portuguese (`pt-PT`),
Dutch (`nl`), Polish (`pl`), Russian (`ru`) and Ukrainian (`uk`). The selector uses
native names with an English identification aid. Flags are not language names.
These are **test translations**; native-speaker review remains pending. Do not
describe draft packs as community-reviewed or production-certified.

Automatic reads Windows preferred UI languages or the Linux message-locale
environment. Explicit supported choices override Automatic. Unsupported or
invalid values fall back safely. `launcher_language` stores a locale code, never
a translated caption or list index. The process C locale is not changed.

UI language is separate from the OS region, local date and holiday calendar.
It does not change ROM/game text, simulation, transport or cross-play identity.
Names, filenames, paths, URLs, friend/lobby codes, player chat and community
content remain in the author's language. Technical diagnostic details and copied
logs remain canonical; classify a diagnostic before translating its summary.

## Resources and ownership

First-party implementation is in `runtime-recomp/src/game/localisation/`.
UTF-8 catalogues and the licensed fallback font are in
`runtime-recomp/assets/localisation/`. `scripts/embed_localisation.py` validates
and embeds data through `runtime-recomp/cmake/Localisation.cmake`; it does not
rewrite application or generated guest sources. The launcher includes the
shared presentation boundaries directly. Do not add an overlay layer or edit
generated guest code or submodules.

English owns stable message IDs and translator contexts. Other packs contain
the same IDs. Use an explicit ID for ambiguous words: launching a game and
resuming the Spotlight carousel have separate messages. The immutable English
alias table supports the retained UI's application-owned literal captions.
Pointer/string data is deliberately not translated implicitly. Audited owned
enum registries resolve their captions each frame; ROM/device/mod names keep
their original bytes.

Visible widget text has an invariant `###` identity derived from the original
caption/internal suffix. Hidden IDs, popup names, field keys, registry IDs and
stored values are never translated. New UI should use explicit stable IDs and
translate only its display caption. Do not translate arbitrary strings merely
because their bytes happen to match an English caption.

## Translating a message

1. Add an English ID, meaningful `context` and `text`; add that ID in every pack.
   Explain the screen, action and any ambiguous terminology. Preserve IDs when
   editing wording. Set `alias: false` for named messages used through `format`
   or `plural`, and give reused short words distinct IDs when their senses differ.
2. Keep every `{named_argument}` exactly once unless the English contract
   explicitly repeats it. Reorder named arguments to suit grammar. Their values
   are inserted once and never looked up as translation keys or format strings.
3. Plural messages have `.one`, `.few`, `.many` and `.other` variants as needed.
   The integer selector follows
   [Unicode CLDR cardinal rules](https://www.unicode.org/cldr/charts/49/supplemental/language_plural_rules.html).
   Check 0, 1, 2, 5, 11, 21, 22, 101 and large counts. Do not build sentences by
   concatenating translated fragments or assume English plurals.
4. New text should use named formatting. Retained `format: "printf"` messages
   must retain the ordered argument types, widths and precision exactly. No
   `%n`, unknown conversions or added format directives are permitted. Ordinary
   prose, including percent signs, is rendered as data.
5. Keep DKR-R, ROM, file extensions, controller button names and project/author
   names identifiable. Translate Mod Library as a free community collection,
   not a shop. Use the same term for the same sidebar page throughout a pack.
   Preserve the distinctions between vanilla, modded, online and development
   saves, and between a friend code and a lobby code.
6. Update the pack's `review` metadata only after an actual review. Record the
   reviewer, scope and date; never infer review from a successful build.

The current font group covers Latin/Cyrillic and required punctuation. CJK,
Arabic/Hebrew and other shaping/RTL groups are deferred until font coverage,
text input, bidirectional layout and appropriate shaping are implemented and
tested. Do not expose an unsupported pack in the selector.

## Validation

Run the catalogue validator and localisation tooling tests, then the required
guard/tooling suite and both platform host test suites. Use
`scripts/localisation_inventory.py` against the final composed UI and its real
include directories to audit presentation call sites without modifying them.
This inventory is an audit aid, not proof that all dynamic messages are covered.
Review owned enum registries, helper boundaries and model-built sentences too.

The authenticated disposable-profile UI harness supports language cycling and
expanded pseudo text. It cannot be armed by normal startup. Check both UI
surfaces at 1280×800, 1920×1080 and 2560×1440, including long labels, dialogs,
keyboard focus, text entry and native script glyphs. Check that atlas dimensions
and texture identity remain unchanged across live switches. Test persistence
after restart and a mixed-language host/client session. Do not treat SDL preview
captures as evidence of actual RT64 overlay rendering.
