# Steam Deck launcher rendering repair

Players reported 15–50 fps across the standalone launcher on Steam Deck LCD,
with Mod Library worst, while Windows and the in-game overlay were smooth.

The Linux branch in `runtime_ui.cpp` explicitly selected SDL's software
renderer unless `DKR_LINUX_ACCELERATED_LAUNCHER=1` was supplied. This applied
to every page. The game overlay uses RT64's renderer instead. Software
sampling and blending of clouds, translucent panels, fonts and cover images
therefore explain the difference between the two surfaces.

The old restriction protected a Vulkan-capable game window from SDL/OpenGL
surface conversion under Gamescope. Removing it alone would risk another
window handoff regression. The repair instead defaults to acceleration on a
fresh launcher window, destroys its renderer before game startup, and creates
a fresh Vulkan-capable window for RT64. Replacement allocation happens before
retiring the original; allocation failure retains the old window. Hidden,
minimised, maximised, size and fullscreen state transfer at the boundary.
The accelerated draw path also skips the CPU solid-triangle/panel adapter.

`DKR_LINUX_ACCELERATED_LAUNCHER=0` remains a troubleshooting override.
Acceleration failure logs its reason and falls back to CPU rendering. The
runtime records SDL's renderer and acceleration flag, plus OpenGL's actual
rasterizer. An accelerated SDL API backed by Mesa llvmpipe is still CPU
rendering; its flag alone is not physical GPU performance evidence.

Qualification includes the owned SDL window/renderer tests, repeated GL ↔
Vulkan window replacements, actual game first presentation, launcher return,
native page captures, host checks and packaged Windows/Linux smoke tests.
Physical Steam Deck LCD frame rate, power and fan behaviour require a Deck
playtest. A 60 Hz redraw target is not a guarantee under arbitrary power caps,
Steam frame limits, thermal throttling or software graphics drivers.
