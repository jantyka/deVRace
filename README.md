# deVRace

A Linux VR-Port of dethrace. I always wanted to play my childhood game in VR so i vibecoded it, dont get mad, i primary did it for me and out of curiosity. A reason why this is Wayland only. Tested with valve index, cuz thats what i have.... If you want to contribute feel free to make a windows or android fork, give me some feature requests or maybe find some bugs. This will be an ongoing project.

Testers and people who would like to port it to Windows or the Meta Quest are very welcome, see [CONTRIBUTING.md](CONTRIBUTING.md).

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
- **Far chase camera** as in other racing games: the view key cycles cockpit, chase and far chase view (`FarChase`).
- **Recenter** the view with the gamepad (right stick press), in the race and in the menus, or from SteamVR's menu.
- **Blood on the lens** when you run over pedestrians at speed: drips run down the lens in the chase views (smaller and fewer in the far one); in the cockpit the airstream pushes it up the windscreen and to the outside of a turn. Moving the mouse too fast in the menus splatters some too. With pixel blood on (`PIXEL BLOOD` in the VR options, `[VR] BloodPixel`), the splatters are the game's own gib sprites instead, turned at random, with their big pixels and without drips or trails.
- **Rear view mirror** in the cockpit view, as a rectangle right below the race timer (`Mirror`).
- **Graphics options at their best** on each start (`MaxDetail`).
- **Comfort vignette** (off by default): the view narrows to a clear circle while the car turns or tumbles fast, and a little at high speed; it closes quickly and opens again slowly.
- **VR options** in the game's options screen (VR button): switch the comfort vignette, the blood on the lens, the options from the slop pack (below) and the post effects (below) on and off, and pick the effect; saved to `dethrace.ini`.
- **Post effects** on the 3D image of each eye, one at a time: `SHARPEN` (contrast adaptive), `VIVID`, `JUNKYARD`, `GLOW` (bloom), `VOODOO` (16-bit colours with dither, like a 3dfx card), `COMIC` (outlines, flat colours) and `CRT` (monitor only). The HUD, mirror and blood are drawn on top, untouched. Off by default; switched in the VR options (`SHADER`, `EFFECT`) or in `[Graphics]`.
- **Cockpit view without the 2D dashboard** and without the driver's hands, showing the car's 3D bonnet. Cars whose data borrows another car's bonnet (most opponents use the Eagle's) show none instead of the wrong one.
- **Smoke, sparks, shadows, culling and cockpit shake** are rendered per eye; the action replay is recorded only once.
- **Electro-bastard ray** drawn in the OpenGL mode, where it was missing (Dethrace pull request [#1131](https://github.com/dethrace-labs/dethrace/pull/1131) by Link4Electronics), with the same zigzag for both eyes.
- **Native Wayland** client through SDL3 and EGL, using `XR_MNDX_egl_enable`. No X11/GLX.
- Frame timing: 144 Hz on the Index with about 2 ms CPU time per frame on the test system.

### Gamepad with force feedback

Any gamepad SDL3 knows (tested: Xbox Wireless Controller over Bluetooth, xpadneo driver). Analog steering, throttle and brake; the keyboard keeps working alongside it. Rumble: light when you run over a pedestrian, strong when you crash into another car.

Default layout (Xbox names), changeable in `[Gamepad]`:

| Gamepad | Race | Menus |
|---|---|---|
| Left stick | Steer (analog) | Navigate |
| RT / LT | Accelerate / brake and reverse (analog) | |
| A | Handbrake | Confirm |
| X (hold) | Repair | Back |
| Y | Recovery | |
| B | Map | |
| RB | Gear down | |
| Left stick press | Horn | |
| Right stick press | VR: recenter the view | VR: recenter the view |
| View (Back) | Cockpit / chase / far chase view | |
| Start | Menu | Back |
| Right stick left / up / right | Look left / forward / right | |
| D-pad down | VR: all HUD elements on/off | Navigate |
| D-pad up | VR: info field at the top (time, laps, credits, messages) on/off | Navigate |
| D-pad left | VR: driver avatar (pratcam) on/off | Navigate |
| D-pad right | VR: all other HUD elements on/off | Navigate |

### From the dethrace-slop-pack

Taken from [dethrace-slop-pack](https://github.com/TRPB/dethrace-slop-pack) by Tom Butler (the commits keep him as their author). The pedestrian spasm fix is always on in the VR build. The extended draw distance is on by default in the VR build; full detail opponents (they kept the headset below 144 Hz at the start of a race on the test system, with all opponents close by) and wheel rounding are off; those can be switched in the VR options or in `[Slop]`:

- **Vastly extended draw distance** covering geometry, opponents, pedestrians and powerups (`ExtendDrawDistance`)
- **Opponent car level of detail always max**, with 10 times the view distance (`OpponentMaxDetail`)
- **Wheel model rounding**: the octagonal wheels are regenerated with 40 sides as the cars load; the one-sided concave wheels of Hawk II and Roadhog are fixed too (`RoundWheels`)
- **Pedestrian spasm fix**: no more pedestrians flipping between two poses every frame (`FixPedSpasm`, always on in the VR build)

The slop pack's other features (widescreen, MSAA and sample rate shading, meld, achievements and more) are not part of deVRace.

### Other additions

- `VSync`, `GrabMouse` and frame timing statistics (`FrameStats`) as general options.

## Requirements

- Linux with a **Wayland** desktop (tested: Arch Linux, GNOME 51)
- An OpenXR runtime: **SteamVR** (tested 2.17.10) or [Monado](https://monado.freedesktop.org/)
- A GPU with OpenGL 4.x through Mesa (tested: Radeon RX 7900 XT, Mesa 26.2)
- A headset (tested: Valve Index)
- Carmageddon game data with `DATA/PIXELMAP/SMOKE.PIX` (the 3dfx assets; the Steam/GOG Max Pack has them)

## Installing (AppImage)

Each [release](https://github.com/jantyka/deVRace/releases) has an AppImage (x86_64) and a setup script. Download both into one folder and run the script:

```sh
chmod +x devrace-setup.sh deVRace-*.AppImage
./devrace-setup.sh
```

The script needs no root and only writes into your home directory. It

- installs the AppImage as `~/Applications/deVRace.AppImage` (and the command `~/.local/bin/devrace`),
- finds Carmageddon (Steam Max Pack, GOG, Heroic; or `--game-dir /path/to/game`) and copies the game data to `~/.local/share/devrace/games/`, because the game writes save games and key maps into its data folder; your Steam/GOG installation is not changed,
- creates `~/.local/share/devrace/dethrace.ini` if there is none,
- adds **deVRace** to the application menu, with the Splat Pack as a second action (right click).

Then start SteamVR and deVRace from the menu. `./devrace-setup.sh --help` lists the options, `--uninstall` removes it again (`--uninstall --purge` also the config and the copied game data). Run without arguments again to update to a newer AppImage; your config and save games are kept.

The AppImage bundles SDL3 and the OpenXR loader. The GPU driver, the OpenXR runtime and the C++ runtime come from your system. It needs FUSE 2 (`libfuse.so.2`; Arch: `fuse2`) to start directly; without it the setup script makes the menu entry unpack it on each start.

Without the script: `./deVRace-*.AppImage --dir /path/to/CARMA` (with a `dethrace.ini` as in [Setup and running](#setup-and-running)).

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

[Graphics]
Shader = 0         ; post effect on the 3D image (1 = on)
PostEffect = sharpen ; sharpen, vivid, junkyard, glow, voodoo, comic or crt (crt only on the monitor)

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
ComfortVignette = 0 ; narrow the view when the car turns or tumbles fast, and a little at high speed
Blood = 1          ; blood may splatter onto the lens when you run over pedestrians at over 70 km/h
BloodPixel = 1     ; the blood is made of the game's gib sprites instead of drawn splatters
Mirror = 1         ; rear view mirror in the cockpit view, below the race timer
MaxDetail = 1      ; set every graphics option to its best on each start
FarChase = 1.8     ; far chase camera: distance as a multiple of the normal one (0 = no far chase camera)

[Slop]
ExtendDrawDistance = 1 ; vastly extended draw distance (yon 1000, pedestrians 20 times as far)
OpponentMaxDetail = 0  ; opponent cars always at full detail, 10 times the view distance
RoundWheels = 0        ; round wheel models (from the next race on)
FixPedSpasm = 1        ; pedestrian spasm fix (ignored in the VR build, always on there)

[Gamepad]
Enabled = 1
; actions: one or two inputs, comma separated. Inputs:
;   A B X Y Back Start LeftStick RightStick LB RB DpadUp DpadDown DpadLeft DpadRight LT RT
;   LeftStickLeft/Right/Up/Down RightStickLeft/Right/Up/Down None
Accelerate = None
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
HudAll = DpadDown
HudInfo = DpadUp
HudAvatar = DpadLeft
HudOther = DpadRight
Recenter = RightStick
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

Environment variables for debugging: `DEVRACE_VR_DEBUG=1` (per-eye diagnostics), `DEVRACE_VR_DUMP_FRAMES=<dir>` (eye images as PPM), `DEVRACE_GAMEPAD_DEBUG=1` (gamepad buttons), `DEVRACE_SHADER_DIR=<dir>` (load the post effect shaders from there and reload them when they change), `DEVRACE_POSTFX_TIMING=1` (GPU time of the post effects). To test without a headset, point `XR_RUNTIME_JSON` to Monado's runtime manifest and run Monado with its simulated (Qwerty) headset.

## Known issues

- The first SteamVR start after connecting the Index may hang for some seconds and restart its compositor; a second start works ([SteamVR-for-Linux#948](https://github.com/ValveSoftware/SteamVR-for-Linux/issues/948)).
- Loading screens pause the headset image briefly.
- The view is centred where your head is when the game starts; to recenter it there is the gamepad (right stick press) or SteamVR's own recentring, no key.
- No 2D dashboard in VR.

## Things planned

- Fixing other missing assets and effects
- Alternative 3D menu
- Inside car models
- Mod support

## Credits

- [Dethrace](https://github.com/dethrace-labs/dethrace) by dethrace-labs and contributors, which this is built on, and everyone credited in its [README](docs/README.dethrace.md)
- [dethrace-slop-pack](https://github.com/TRPB/dethrace-slop-pack) by Tom Butler for the extended draw distance, full detail opponents, round wheels and the pedestrian spasm fix
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
