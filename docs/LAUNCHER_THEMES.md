# Launcher themes

Hovered or controller-focused buttons use the active season's pattern in
the launcher and overlay. Halloween has spiderwebs, Bonfire Night has fireworks
and Christmas has wrapping paper, ribbons and bows. Labels keep their dark
backing so they stay readable.

Winter uses snowflakes; spring has flowers; summer has waves and sunshine;
autumn has leaves. New Year adds confetti, Valentine's Day hearts, St Patrick's
Day shamrocks and Easter decorated eggs. Lunar New Year and Eid al-Fitr use
lanterns, Diwali has diya lights, and Eid al-Adha and Hanukkah use distinct star
patterns. Thanksgiving uses harvest shapes. National celebrations use US stars
or Canadian maple leaves according to the detected region. Original keeps the
racing checks. The patterns follow the existing region and calendar detection,
including the existing limits on supported festival dates.

The original rendered corner decorations also appear on the overlay. Button
patterns are cached with the UI artwork. They do not download images or create
artwork while racing. Turning off animations keeps the pattern still.

The launcher changes with the time of year! It keeps the scrolling clouds,
with seasonal colours, a little background animation and rendered decorations
in the top left and bottom right. The menus keep their usual look.

There is nothing to set up in About DKR-R. Your computer's region and local
date pick the theme. Old manual theme, calendar, hemisphere and event choices
are ignored, so an old saved choice won't leave the launcher stuck on Halloween.

## Region and date

Windows uses **Settings > Time & language > Language & region > Country or
region**. If that is unavailable, it uses the territory in the Windows locale.
It doesn't choose a country just because the display language is English.

Linux and Steam Deck first read the user's regional formats from KDE's
`plasma-localerc` in `XDG_CONFIG_HOME` or `~/.config`. They then try regional
`LC_ALL`, `LC_ADDRESS` and `LC_TIME`, `/etc/locale.conf` or `/etc/default/locale`,
and finally the territory in `LANG`. Generic `C`, `C.UTF-8` and `POSIX` values
allow fallback instead of suppressing the calendar in Gaming Mode. Files are
read as bounded data; no shell configuration is executed.

For example, `en_GB.UTF-8` selects the UK calendar and `en_AU.UTF-8` selects
southern seasons. An unknown country uses international events, such as
Halloween and Christmas, without guessing a hemisphere or regional holiday.
The runtime log reports the detected territory and source. Detection is cached
for 60 seconds and refreshed on focus/resume. There is no network lookup.
Physical Steam Deck Gaming Mode testing remains part of playtester validation.

The launcher checks when it opens, when you return to it and once a minute
while visible. The local calendar day is used, including after a time-zone
change. Switching themes does not leave your current page or reload the atlas.

Northern seasons use December–February for Winter, March–May for Spring,
June–August for Summer and September–November for Autumn. Countries mapped to
southern seasons include Australia, New Zealand, South Africa, Argentina,
Brazil, Chile and the other supported southern territories. Countries spanning
the equator use one country-level convention; this isn't a local weather service.

## Events and artwork

There are 18 seasonal/event designs. Each gets its own pair of decorations.
National celebrations have separate US and Canadian flags with matching bunting.
The original blue-cloud theme has no corner decorations.
During an active holiday, Play also shows a greeting between its title and
Check for updates. The message follows that same event and region. It wraps
on smaller screens and stays hidden during ordinary seasonal themes.

| Theme | Top left | Bottom right |
| --- | --- | --- |
| Winter | Mitten | Snowflake |
| Spring | Flower | Butterfly |
| Summer | Sun | Beach ball |
| Autumn | Leaf | Acorn |
| Halloween | Jack-o'-lantern | Bat |
| Bonfire Night | Bonfire | Firework rocket |
| Christmas | Decorated tree | Present |
| New Year | Party hat | Confetti popper |
| Valentine's Day | Heart | Letter |
| St Patrick's Day | Shamrock | Green hat |
| Easter | Painted egg | Rabbit |
| Lunar New Year | Red lantern | Lucky envelope |
| Diwali | Diya lamp | Rangoli flower |
| Eid al-Fitr | Lantern | Sweets |
| Eid al-Adha | Crescent | Gift basket |
| Hanukkah | Nine-light hanukkiah | Dreidel |
| Thanksgiving | Harvest pumpkin | Wheat |
| National celebrations | Regional flag | Matching bunting |

Bonfire Night uses the UK region. Thanksgiving and national celebrations use
the US or Canadian region. St Patrick's Day is enabled for GB, Ireland, the
US, Canada, Australia and New Zealand.

Lunar New Year is enabled for CN, HK, MO, TW, KR, VN, SG and MY. Diwali is
enabled for IN, SG, MY, MU, FJ and NP. Eid decorations are enabled for AE,
BH, BD, BN, DZ, EG, ID, IN, IQ, IR, JO, KW, LB, LY, MA, MY, OM, PK, PS,
QA, SA, SD, SG, TN, TR and YE. Hanukkah uses IL. These are decorative calendar
choices based on the configured country, not a statement about the player.

## Decoration windows

All windows include their first and last dates. They are launcher decoration
windows, rather than a public holiday or religious observance calendar.

| Theme | Window |
| --- | --- |
| Halloween | 20–31 October |
| Bonfire Night | 1–7 November, UK |
| Christmas | 1–28 December |
| New Year | 29 December–3 January |
| Valentine's Day | 10–16 February |
| St Patrick's Day | 15–18 March |
| Easter | The Sunday before Gregorian Easter through Easter Monday |
| Lunar New Year | Three days before through five days after the listed first day |
| Diwali | Two days before through two days after the listed date |
| Eid al-Fitr | The listed date and the following two days |
| Eid al-Adha | The listed date and the following three days |
| Hanukkah | The published starting civil date through the ending civil date |
| Thanksgiving, US | Two days before the fourth Thursday in November through three days after |
| Thanksgiving, Canada | The Saturday before the second Monday in October through Tuesday |
| National celebrations, US | 2–5 July |
| National celebrations, Canada | 1–2 July |

Regional events take priority when windows overlap. Their order is Lunar
New Year, Diwali, Eid al-Fitr, Eid al-Adha, Hanukkah, Thanksgiving and National
celebrations. The other events follow the order New Year, Christmas, Halloween,
Bonfire Night, Valentine's Day, St Patrick's Day and Easter. Seasons fill the gaps.

Lunar New Year, Diwali and Eid have verified dates for **2026–2027**. Hanukkah
has dates for **2026–2032**. Outside that coverage, the launcher uses the next
applicable event or season. Extending these tables needs a launcher update;
the launcher doesn't fetch calendars from a server or invent future lunar dates.

Regional dates can differ between communities. The Diwali and Eid reference
dates use Singapore's published calendar. These are local civil days, including
Hanukkah, rather than sunset calculations or a claim of exact local observance.

Date references:

- [Singapore Ministry of Manpower, 2026 public holidays](https://www.mom.gov.sg/newsroom/press-releases/2025/0616-public-holidays-for-2026)
- [Singapore Ministry of Manpower, 2027 public holidays](https://www.mom.gov.sg/newsroom/press-releases/2026/0618-public-holidays-for-2027)
- [Hong Kong Observatory, 2027 Gregorian–lunar conversion](https://www.hko.gov.hk/en/gts/time/calendar/pdf/files/2027e.pdf)
- [Hebcal, Hanukkah dates](https://www.hebcal.com/holidays/chanukah-2027)
- [GOV.UK, Easter bank holiday dates](https://www.gov.uk/bank-holidays)
- [US Office of Personnel Management, holiday dates](https://www.opm.gov/policy-data-oversight/pay-leave/federal-holidays/)
- [Library and Archives Canada, holiday calendar](https://www.canada.ca/en/library-archives/services/public/visit/holiday-closures.html)

## Motion and performance

The artwork is original interface art made from the approved models. No game
assets are included. The launcher loads one embedded atlas for each SDL renderer
and draws two image quads per frame. There is no live 3D rendering, per-frame
image decoding, download or extra shader work for the decorations.

Windows' reduced-animation setting stops background motion. Existing saved
animation opt-outs are kept on both platforms. Background motion also pauses
while the launcher is unfocused or hidden. These effects run in the launcher only.

Policy, region detection, artwork and drawing live in
`runtime-recomp/src/game/launcher_themes/`. The existing retained UI adapter
connects them while the source fold is in progress. No new patch layer, guest
hook or online compatibility identity is needed.

Checks cover locale precedence, unknown regions, northern/southern seasons,
old settings, date boundaries, overlap, midnight/resume, artwork selection,
transparent sprite borders, atlas lifetime and desktop/Deck-sized rendering.
