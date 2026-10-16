# deVRace

A Linux VR-Port of dethrace. I always wanted to play my childhood game in VR so i vibecoded it, dont get mad, i primary did it for me and out of curiosity. A reason why this is Wayland only. Tested with Valve Index, cuz thats what i have.... If you want to contribute feel free to make a Windows or Android Fork, give me some feature requests or maybe find some bugs. This will be an ongoing project.

---

deVRace is a fork of [Dethrace](https://github.com/dethrace-labs/dethrace), the reimplementation of the 1997 game [Carmageddon](https://en.wikipedia.org/wiki/Carmageddon). It adds a VR mode through [OpenXR](https://www.khronos.org/openxr/), a gamepad with force feedback, and a few options around them. Without the VR build option it builds and runs like Dethrace.

You need your own copy of Carmageddon (e.g. the Max Pack on [Steam](https://store.steampowered.com/app/282010/) or [GOG](https://www.gog.com/game/carmageddon_max_pack)). No game data is included.

The original Dethrace README is in [docs/README.dethrace.md](docs/README.dethrace.md).

## Features

### VR (OpenXR)

- **Stereo 3D with head tracking** on top of the game's own cameras. The chase and cockpit cameras behave as in the original (no smoothing, no comfort changes); your head pose is added to them.
- **Per-eye asymmetric projection** from the OpenXR runtime, rendered by Dethrace's OpenGL ("3dfx") renderer into one render target per eye.
- **Sky fixed to the world**: the original sky strip is rebuilt as a cylinder around you, so it stays put when you look around.
- **Menus and HUD on a panel** in front of you (OpenXR quad layer). In the race the HUD floats over the 3D view; every HUD element can be switched on and off.
- **Cockpit view without the 2D dashboard**, showing the car's 3D bonnet. Cars whose data borrows another car's bonnet (most opponents use the Eagle's) show none instead of the wrong one.
- **Smoke, sparks, shadows, culling and cockpit shake** are rendered per eye; the action replay is recorded only once.
- **Native Wayland** client through SDL3 and EGL, using `XR_MNDX_egl_enable`. No X11/GLX.
- Frame timing: 144 Hz on the Index with about 2 ms CPU time per frame on the test system.

### Gamepad with force feedback

Any gamepad SDL3 knows (tested: Xbox Wireless Controller over Bluetooth, xpadneo driver). Analog steering, throttle and brake; the keyboard keeps working alongside it. Rumble: light when you run over a pedestrian, strong when you crash into another car.

Default layout (Xbox names), changeable in `[Gamepad]`:

| Gamepad | Race | Menus |
|---|---|---|
| Left stick | Steer (analog) | Navigate |
| RT / LT | Accelerate / brake and reverse (analog) | |
| A | Accelerate | Confirm |
| X (hold) | Repair | Back |
| Y | Recovery | |
| B | Map | |
| LB / RB | Handbrake / gear down | |
| Left stick press | Horn | |
| View (Back) | Cockpit / chase view | |
| Start | Menu | Back |
| Right stick left / up / right | Look left / forward / right | |
| D-pad down | VR: all HUD elements on/off | Navigate |
| D-pad up | VR: info field at the top (time, laps, credits, messages) on/off | Navigate |
| D-pad left | VR: driver avatar (pratcam) on/off | Navigate |
| D-pad right | VR: all other HUD elements on/off | Navigate |

### Other additions

- `VSync`, `GrabMouse` and frame timing statistics (`FrameStats`) as general options.

## Requirements

- Linux with a **Wayland** desktop (tested: Arch Linux, GNOME 51)
- An OpenXR runtime: **SteamVR** (tested 2.17.10) or [Monado](https://monado.freedesktop.org/)
- A GPU with OpenGL 4.x through Mesa (tested: Radeon RX 7900 XT, Mesa 26.2)
- A headset (tested: Valve Index)
- Carmageddon game data with `DATA/PIXELMAP/SMOKE.PIX` (the 3dfx assets; the Steam/GOG Max Pack has them)

## Building

Packages (Arch Linux):

```sh
sudo pacman -S --needed base-devel git cmake ninja sdl3 openxr mesa
```

Clone with the submodule (BRender, with deVRace's changes) and build:

```sh
git clone --recurse-submodules https://github.com/jantyka/deVRace
cd deVRace
cmake -B build-vr -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DDETHRACE_PLATFORM_SDL2=OFF -DDETHRACE_PLATFORM_SDL3=ON -DDETHRACE_VR=ON
cmake --build build-vr
```

The result is `build-vr/devrace`. Nothing is installed system-wide. Without `-DDETHRACE_VR=ON` you get a regular Dethrace (`dethrace`).

## Setup and running

1. Copy the game data somewhere, e.g. the Steam installation's `CARMA` folder (Carmageddon) and `CARSPLAT` folder (Splat Pack).
2. Create `~/.local/share/devrace/dethrace.ini` (deVRace keeps its own config, an existing Dethrace config is not touched):

   ```ini
   [General]
   Emulate3DFX = 1
   Hires = 1
   PhysicsPerFrame = 1
   FPSLimit = 0
   Windowed = 1
   Cutscenes = 0
   DefaultGame = c1

   [Games]
   c1 = /path/to/CARMA
   sp = /path/to/CARSPLAT
   ```

   `Emulate3DFX = 1` is required: VR uses the OpenGL renderer.
3. Start SteamVR from Steam, then the game:

   ```sh
   SDL_VIDEODRIVER=wayland ./build-vr/devrace
   ```

   The game window stays on the monitor; the headset shows the game. If OpenXR cannot be started, the game runs on the monitor only.

The game's own keyboard layout uses the number pad for driving. You can change it in the game's options (e.g. to WASD).

## Configuration

All options go into `~/.local/share/devrace/dethrace.ini`. The general Dethrace options are described in [docs/CONFIGURATION.md](docs/CONFIGURATION.md). deVRace adds:

```ini
[General]
VSync = 1          ; wait for the monitor (turned off automatically in VR, the headset sets the pace)
GrabMouse = 0      ; keep the mouse in the window (turned on automatically in VR)
Gamepad = 1        ; use a connected gamepad

[Developers]
FrameStats = 0     ; log frame timing every N seconds (0 = off)

[VR]
HudDistance = 1.8  ; HUD/menu panel: distance in metres
HudWidth = 1.6     ; HUD/menu panel: width in metres
Hud = 1            ; HUD during the race (menus are always shown)
HudDimming = 1     ; single HUD elements (1 = on, 0 = off)
HudDamage = 1
HudPratcam = 1
HudMessages = 1
HudInstruments = 1
HudPowerups = 1
HideBorrowedBonnet = 1 ; hide bonnets a car borrows from another car in the cockpit view

[Gamepad]
Enabled = 1
; actions: one or two inputs, comma separated. Inputs:
;   A B X Y Back Start LeftStick RightStick LB RB DpadUp DpadDown DpadLeft DpadRight LT RT
;   LeftStickLeft/Right/Up/Down RightStickLeft/Right/Up/Down None
Accelerate = A
Handbrake = LB
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
HudAll = DpadDown
HudInfo = DpadUp
HudAvatar = DpadLeft
HudOther = DpadRight
MenuConfirm = A
MenuBack = X, Start
; analog axes: LeftStickX RightStickX LT RT None
Steer = LeftStickX
Throttle = RT
Brake = LT
Deadzone = 0.2
Rumble = 1
RumblePedestrian = 0.35
RumbleCar = 1.0
```

Environment variables for debugging: `DEVRACE_VR_DEBUG=1` (per-eye diagnostics), `DEVRACE_VR_DUMP_FRAMES=<dir>` (eye images as PPM), `DEVRACE_GAMEPAD_DEBUG=1` (gamepad buttons). To test without a headset, point `XR_RUNTIME_JSON` to Monado's runtime manifest and run Monado with its simulated (Qwerty) headset.

## Known issues

- The first SteamVR start after connecting the Index may hang for some seconds and restart its compositor; a second start works ([SteamVR-for-Linux#948](https://github.com/ValveSoftware/SteamVR-for-Linux/issues/948)).
- Car shadows are not drawn in the 3dfx mode (also not on the monitor).
- Loading screens pause the headset image briefly.
- No recenter key yet; the view is centred where your head is when the game starts.
- No rear view mirror and no 2D dashboard in VR.

## Things planned

- Fixing missing assets, like the lightning strike etc.
- Alternative 3D menu
- Inside car models
- Mod support

## Credits

- [Dethrace](https://github.com/dethrace-labs/dethrace) by dethrace-labs and contributors, which this is built on, and everyone credited in its [README](docs/README.dethrace.md)
- [BRender](https://github.com/dethrace-labs/BRender-v1.3.2) by Argonaut Software, as maintained for Dethrace
- Stainless Software and SCi for Carmageddon
- [OpenXR](https://www.khronos.org/openxr/) by the Khronos Group, the [OpenXR SDK](https://github.com/KhronosGroup/OpenXR-SDK-Source) and its validation layers
- [Monado](https://monado.freedesktop.org/) by Collabora and contributors, used for testing without a headset
- [SDL](https://www.libsdl.org/) for windowing, input and gamepads
- References that helped: the [Khronos OpenXR Tutorial](https://openxr-tutorial.simul.co/), [openxr-simple-example](https://gitlab.freedesktop.org/monado/demos/openxr-simple-example), the LWJGL OpenXR sample [`XRHelper.java`](https://github.com/LWJGL/lwjgl3/blob/master/modules/samples/src/test/java/org/lwjgl/demo/openxr/XRHelper.java) and Godot's use of `XR_MNDX_egl_enable` on Wayland, [Linux VR Adventures](https://lvra.gitlab.io/), the [SteamVR for Linux](https://github.com/ValveSoftware/SteamVR-for-Linux) issue tracker
- Written with the help of [Claude Code](https://claude.com/claude-code)

## Legal

deVRace is a fork of Dethrace and is distributed under the same terms; see [LICENSE](LICENSE) and the legal note of the original project: "The documentation and function provided by Dethrace may only be utilized with assets provided by ownership of Carmageddon." BRender is released under the MIT license ([lib/BRender-v1.3.2/LICENSE](https://github.com/jantyka/BRender-v1.3.2/blob/vr-openxr/LICENSE)).

deVRace is not affiliated with or endorsed by the rights holders of Carmageddon, Stainless Software or Valve. Carmageddon is a trademark of its respective owners.
