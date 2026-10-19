# Configuration

Dethrace looks for a `dethrace.ini` file in the [system preferences directory](https://wiki.libsdl.org/SDL2/SDL_GetPrefPath):
| Platform | Example Path                                                                 |
|----------|----------------------------------------------------------------------|
| Windows  | `C:\Users\<NAME>\AppData\Roaming\dethrace\dethrace.ini`                |
| macOS    | `/Users/<NAME>/Library/Application Support/dethrace/dethrace.ini`      |
| Linux    | `/home/<NAME>/.local/share/dethrace/dethrace.ini`                      |


If this file is not present, dethrace will run with default configuration and attempt to discover the correct Carmageddon directory to use (see below).

## Example dethrace.ini file
```ini
[General]
; Enable original CD check
CdCheck = 0

; Enable original censorship check
GoreCheck = 0

; set to 0 to disable
FPSLimit = 60

; Full screen or window
Windowed = 1

; 3dfx mode (via OpenGL)
Emulate3DFX = 0

; Censored zombie/robots mode
BoringMode = 0

; Play cut scenes on startup and between races
Cutscenes = 0

; "hires" mode is 640x480, otherwise default 320x200
Hires = 1

; Only used in 'demo' mode. Default demo time out is 240s (4 mins)
DemoTimeout = 240

; Which directory in the [Games] section to run
DefaultGame = c1

; Use a connected gamepad (layout in src/DETHRACE/pc-all/allgamepad.c, changeable in [Gamepad])
Gamepad = 1

; Original physics updates every 40ms, causing noticable camera juddering when cornering at slow speed
; Enabling this runs physics every frame, smoothing movement out
PhysicsPerFrame = 1

[Games]
c1 = /opt/carma/c1
c1demo = /opt/carma/c1demo
sp = /opt/carma/splatpack

[Cheats]
EditMode = 0
FreezeTimer = 0
GameCompleted = 0

[Sound]
Enabled = 1
SoundOptionsScreen = 1
VolumeMultiplier = 1

[Network]
AdapterName = ""

; Gamepad layout (SDL3 platform only), see "Gamepad" below
[Gamepad]
Steer = LeftStickX
Throttle = RT
Brake = LT
Deadzone = 0.2
Handbrake = A
GearDown = RB
Horn = LeftStick
Recovery = Y
Repair = X
Cockpit = Back
Map = B
Menu = Start
LookLeft = RightStickLeft
LookForward = RightStickUp
LookRight = RightStickRight
Pratcam = DpadLeft
MenuConfirm = A
MenuBack = X, Start
Rumble = 1
RumblePedestrian = 0.35
RumbleCar = 1.0
```

## Gamepad

With the SDL3 platform, the first connected gamepad is used (`Gamepad = 0` in `[General]` turns it off). Steering, throttle and brake are analog; the keyboard keeps working alongside it. In the menus the d-pad and the left stick move like the arrow keys, `MenuConfirm` is Enter and `MenuBack` is Escape. The gamepad rumbles when running over a pedestrian and when crashing into another car.

Each action takes one or two inputs separated by a comma, or `None`. Inputs (Xbox names): `A`, `B`, `X`, `Y`, `Back`, `Guide`, `Start`, `LeftStick`, `RightStick` (stick presses), `LB`, `RB`, `DpadUp`, `DpadDown`, `DpadLeft`, `DpadRight`, `LeftStickLeft`/`Right`/`Up`/`Down`, `RightStickLeft`/`Right`/`Up`/`Down`, `LT`, `RT`. Actions: `Accelerate` (digital, in addition to `Throttle`), `Handbrake`, `GearDown`, `Horn`, `Recovery`, `Repair`, `Cockpit`, `Map`, `Menu`, `LookLeft`, `LookForward`, `LookRight`, `Pratcam`, `MenuConfirm`, `MenuBack`. Axes for `Steer`, `Throttle` and `Brake`: `None`, `LeftStickX`, `RightStickX`, `LT`, `RT`. `DETHRACE_GAMEPAD_DEBUG=1` logs every button change and the keys it sets.

## Order of precedence for game directory detection:
1. Directory pointed to by DefaultGame
2. First game in the list if at least 1 game dir is specified
3. `--dir` command line argument
4. Current working directory (if `DATA/GENERAL.TXT` exists)
5. `SDL_GetPrefPath` directory
