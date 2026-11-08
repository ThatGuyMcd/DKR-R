Analogue Acceleration & Braking

Requires DKR-R v1.0.14 or newer.

Import the .nrm in Installed Mods > My mods > Custom Magic Codes and enable
it. Open this mod's Details, select your local player and apply the RT / LT
driving layout. RT accelerates, LT brakes and LB uses items. Check the live input bars.

Both pedals are independent. Full input and release use the native vehicle
behaviour; partial input caps the native throttle/brake buildup. The mod does
not set vehicle speed. It covers cars, hovercraft and planes. CPU racers and
special vehicles keep their normal controls.

For online play, the host enables the mod and everyone uses compatible v1.0.14 builds. Players
can use different bindings and calibrations. Analogue values are recorded and
replayed with the rest of each player's input.

Build from the SDK folder:
python build.py examples/analogue-driving --output OUT --mod-tool RecompModTool

Pass --clang and --lld if those tools are not on PATH. Choose a new output
folder. The source and dkr/driving.h are included for SDK authors.

Companion version 1.0.1 updates the setup wording and stable minimum version.
The authored driving program is unchanged from the accepted Beta 3 companion.
