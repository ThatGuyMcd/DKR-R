# DKR water colour brightness

The first DKR colours build was too restrained. The user's two Crescent Island
screenshots show that it changes the water, but the foreground is still muted
teal. The setting is reaching the material; this is a palette tuning issue.

The shader lights the diffuse body with a factor of 0.5 or less, then applies
tone compression and the display transfer. The first palette only increased
the scene-linear colours modestly, leaving too little visible separation from
Current HD. Refraction and reflected scenery also contribute to the result.

The normal first-party water style policy now uses substantially brighter
scene-linear turquoise values for daylight water and a brighter cold palette
for snow. Values above one are intentional lighting inputs, not display RGB.
Dark courses retain their darker palette. Custom authored profiles remain intact.

Only the stock DKR body colours change. The shader, reflection sampling,
reflection strength, Fresnel response, normals, absorption, transparency, fog,
wave behaviour and resource budgets remain unchanged from the previous test.
Current HD, lava, waterfalls, fountains and Original water are unchanged.

The policy test now requires a visible luminance separation from the first DKR
palette under representative lighting and the actual shader's display transfer.
It also checks highlight headroom. This is a numeric palette acceptance check;
it is not a matched GPU screenshot comparison. The existing optical invariants
and custom-profile checks remain in place.

## Verification

Both production builds compiled and their packages passed content audits,
worker probes and archive readback. Only the runtime executable differs from
the previous package on each platform. Compatibility IDs are unchanged.
The shader patches, shader authoring scripts, settings UI, projection guard
and material adapter match the preceding Water Style Test byte for byte.

Windows passed all 150 host tests. Linux passed 150 of 151 on its full run;
the regression harness hit four subprocess timeouts during concurrent checks.
That same tooling test passed on its own in 98 seconds. The original result
and the passing retry are both retained.
The water policy target passed separately on both platforms, including the
new contrast bounds. Guard/tooling and changed-file pre-commit checks passed.

The final Windows package ran for 65 seconds with enhanced water active:
1,073 profiled water frames carried the style flag, with a clean shutdown.
The actual AppImage ran under Xvfb and software Vulkan for 65 seconds:
267 water frames carried the flag, with a clean shutdown. These runs verify
the active draw path and lifecycle, not matched image quality or GPU speed.
The user should compare both styles in the same scene at the same reflection
settings. No real Steam Deck/GPU visual acceptance is claimed.
