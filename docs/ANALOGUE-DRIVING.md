# Analogue acceleration and braking

Requires DKR-R v1.0.14 or newer and the optional
**Analogue Acceleration & Braking** Custom Magic Code.

1. Import the `.nrm` in Installed Mods > My mods > Custom Magic Codes and enable it.
2. Open the mod's **Details** and pick your local player.
3. Apply the RT / LT driving layout. RT accelerates, LT brakes and LB uses
   items. The preset changes the conflicting item and L bindings.
4. Watch the two input bars. Adjust Release deadzone, Full travel, Response
   curve or Invert axis if your controller needs it.

Each player has their own bindings and calibration. Keyboard keys and ordinary
controller buttons still give released or full input. A trigger shared with
the original accelerator binding keeps its continuous value.

Half throttle is a request for less driving effort, not a speed limit. Native
vehicle physics still handles momentum, boosts, braking buildup and coasting.
Reverse and the usual combinations of buttons stay available. This covers the
three standard vehicle types; CPU racers and special boss vehicles keep their
original controls. Other gameplay mods that replace vehicle updates need
separate compatibility testing.

For Online, the host enables the gameplay mod. Each player then chooses their
own controls in the mod's Details. A temporary online copy has its own Details
card in Custom Magic Codes, even if you have not installed it offline. These
controller settings stay available during a game or lobby and save on your
device. Existing per-player bindings and calibration are kept.

Use matching compatible v1.0.14 packages. Analogue pedal channels were added
during Beta 3; earlier beta input formats are incompatible.
Rollback and spectators use the same recorded values.

Look behind is separate. Enable it in Graphics > HUD & effects and map Look
back in Controls > Button mapping. Hosts and clients control their own racer
view. ModernCamera++ is optional. Shared menus and scripted cameras keep their
normal view.

SDK authors can include `dkr/driving.h` and require `analogue-driving` version
1 in an API 2 mod. `DkrDrivingInput` is a separate 20-byte record; API 1
`DkrInput` remains unchanged. Values are calibrated integers from 0 to 255,
with independent accelerator and brake levels. Read and apply them from
`on_frame`. The service applies the recorded sample for that tick, including
replayed ticks, and never reads a physical controller from simulation.

The complete example is in `modding/examples/analogue-driving`. Build it with
the SDK's `build.py`, Clang/LLD and the matching RecompModTool. The package is
optional and is never enabled automatically.
