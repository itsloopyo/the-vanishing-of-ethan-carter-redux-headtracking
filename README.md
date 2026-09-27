# The Vanishing of Ethan Carter Redux Head Tracking

![The Vanishing of Ethan Carter Redux running with this mod](https://raw.githubusercontent.com/itsloopyo/the-vanishing-of-ethan-carter-redux-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for The Vanishing of Ethan Carter Redux that moves the view with your head while your mouse or controller keeps control of look and interaction, driven by a webcam, phone, or any OpenTrack compatible tracker, with no VR headset required.

## Features

- **Decoupled look and aim** - your head moves the view, your mouse or controller still controls where you look and interact.
- **6DOF tracking** - rotation and position, so you can lean in and peek around what the level puts in your way.
- **Works with any OpenTrack compatible tracker** - free options available for PC, iOS and Android

## Requirements

- [The Vanishing of Ethan Carter Redux](https://store.steampowered.com/app/258520/), the Steam
  Win64 build (`EthanCarter-Win64-Shipping.exe`, PE build date 2015-09-15).
- A tracking source: [OpenTrack](https://github.com/opentrack/opentrack/releases), or any app that sends the OpenTrack UDP protocol.
- Windows 10 or 11, 64-bit.

The mod checks the game executable against the build it knows before it touches
anything. On a build it does not recognise it installs no hooks at all, writes a
line to `HeadTracking.log` saying it is staying dormant and whether your build
looks newer or older than the one it knows, and the game runs exactly as it does
without the mod. Nothing is patched on disk either way.

## Installation

### Lopari

Download [Lopari](https://lopari.app), choose **The Vanishing of Ethan Carter Redux**, and click
**Play with head tracking**.

### Standalone Installer

1. Download the latest `EthanCarterReduxHeadTracking-v<version>-installer.zip` from the [Releases](https://github.com/itsloopyo/the-vanishing-of-ethan-carter-redux-headtracking/releases) page.
2. Extract it anywhere.
3. Double-click `install.cmd`.
4. Configure OpenTrack to output UDP to `127.0.0.1:4242` (see below).
5. Launch the game.

If the installer cannot find your game, tell it where the game is. Either set
the path in an environment variable:

```powershell
$env:ETHAN_CARTER_REDUX_PATH = "D:\Games\The Vanishing of Ethan Carter Redux"
.\install.cmd
```

Or pass it as the first argument:

```powershell
.\install.cmd "D:\Games\The Vanishing of Ethan Carter Redux"
```

### Manual Installation

To place the files by hand instead:

1. Copy `vendor\ultimate-asi-loader\dinput8.dll` from the installer ZIP into
   `<game>\EthanCarter\Binaries\Win64\`, renamed to `winmm.dll`. A proxy DLL is
   only loaded if the game's own executable imports that name, and this one
   imports `winmm.dll`. It also imports `dwmapi.dll`, but UE4SS takes that if you
   have it installed, which is why this mod does not use it.
2. Copy `plugins\EthanCarterReduxHeadTracking.asi` into the same folder,
   alongside `EthanCarter-Win64-Shipping.exe`.

The `-nexus.zip` release archive holds the same `.asi` already under the
`EthanCarter\Binaries\Win64\` path it belongs in, but no loader. Whichever
archive you start from, `winmm.dll` has to be in place or nothing loads.

`CameraUnlock.ini` is written next to the `.asi` on first launch, so there is
nothing to copy for it.

## Setting Up OpenTrack

The mod listens for OpenTrack pose data on UDP port `4242`. OpenTrack's **UDP
over network** output sends exactly that.

1. Install [OpenTrack](https://github.com/opentrack/opentrack/releases).
2. Pick a tracker under **Input** (see the subsections below).
3. Set **Output** to **UDP over network**, host `127.0.0.1`, port `4242`.
4. Press **Start**. The tracker and the game can be started in either order.

Centering is done in your tracker: OpenTrack's **Center** bind, or the CENTER
button in a phone app. The mod applies the pose it receives exactly as it
arrives.

### VR Headset Setup

A headset can drive this mod like any other source, as long as OpenTrack can
read its runtime. Connect the headset the way you normally do, then look through
OpenTrack's **Input** dropdown for an entry matching it and use the output
settings above. Which inputs your OpenTrack build ships depends on the version,
so check that dropdown before setting anything else up.

### Webcam Setup

OpenTrack ships a `neuralnet tracker` input that reads a plain webcam, with no
markers and no IR hardware. Select it under **Input**, pick your camera in its
settings, and use the output settings above. How well it tracks depends on your
camera and your lighting, so try it before buying anything.

### Phone App Setup

The mod accepts one thing: the OpenTrack UDP protocol on port `4242`. A phone
app is usable here if it sends that itself, or if it ships a PC-side companion
that does. Check your app for an OpenTrack or UDP output option before assuming
it will work.

For an app that does send it, what decides the wiring is how much filtering the
app does before the packet leaves the phone. An app that filters on-device can
point straight at this PC's LAN address (run `ipconfig` to find it) on port
`4242`. A raw or lightly filtered feed sent direct will jitter, because the
mod's smoothing is sized to take the edge off a clean signal rather than to
rescue a noisy one. The test is quicker than the theory: try direct, hold your
head still, and if the view drifts or shakes, point the app at OpenTrack's
**UDP over network** *input* on some other port instead, say `5252`, and let
OpenTrack's filters and curves clean the feed up before its own output forwards
to `127.0.0.1:4242`.

I made [Headcam](https://headcam.app) so decent tracking was free for anybody
with a phone already in their pocket, and it filters on-device, so it can send
direct. Another app that sends the OpenTrack protocol and filters on-device can
do the same; the test above is how you find out.

A phone on WiFi is a remote connection and gets `RemoteSmoothing`. So does a
tracker running on this same PC that sends to the machine's LAN address instead
of `127.0.0.1`, because the mod reads the source address of the packet and not
the machine it came from.

## Controls

Two equivalent binding sets. Use whichever your keyboard has. Both are the
defaults of the key lists in `CameraUnlock.ini` (see Configuration), where each
action can be given other keys:

| Action | Nav-cluster | Chord |
|--------|-------------|-------|
| Toggle tracking | `End` | `Ctrl+Shift+Y` |
| Cycle tracking mode | `Page Up` | `Ctrl+Shift+G` |
| Toggle yaw mode (world/local) | `Page Down` | `Ctrl+Shift+H` |

Cycling the tracking mode steps through full tracking, then rotation only, then
position only, then back to full.

The tracking mode and the yaw mode are saved to `CameraUnlock.ini` the moment
you change them, so the game starts in them next time. Toggling tracking with
`End` lasts for the session only; whether tracking starts on is
`EnableOnStartup`.

## Configuration

<!-- cameraunlock:config -->
The mod reads its settings from `EthanCarter\Binaries\Win64\CameraUnlock.ini` in the game folder, and creates the file when it starts and finds none. Edit it with any text editor.

A setting set to `default` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.

`Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.

When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that. Edit it with any text editor.

The built-in value of each setting set to `default` below:

- `UdpPort=4242`
- `EnableOnStartup=true`
- `WorldSpaceYaw=true`
- `RotationEnabled=true`
- `LocalSmoothing=0.0`
- `RemoteSmoothing=0.15`
- `PositionEnabled=true`
- `PositionLimitX=0.3`
- `PositionLimitY=0.2`
- `PositionLimitYDown=0.2`
- `PositionLimitZ=0.4`
- `PositionLimitZBack=0.1`
- `CollisionEnabled=true`
- `CollisionReleaseSmoothing=0.9`
- `ToggleKey=End, Ctrl+Shift+Y`
- `CycleTrackingModeKey=PageUp, Ctrl+Shift+G`
- `YawModeKey=PageDown, Ctrl+Shift+H`

With every setting at its default, the file reads:

```ini
; The Vanishing of Ethan Carter Redux head tracking settings.
; Comments start with ; and go on their own line. Text after a value is part of the value.
; Hotkeys are key names such as End, PageUp or Ctrl+Shift+Y. Separate several with commas; leave empty for none.
; A setting set to default takes its value from Defaults.ini, which every head tracking mod
; that keeps its settings in CameraUnlock.ini reads: %AppData%\CameraUnlock\Defaults.ini on
; Windows, $XDG_CONFIG_HOME/CameraUnlock/Defaults.ini (normally ~/.config/CameraUnlock) on
; Linux, under Wine and Proton too, and ~/Library/Application Support/CameraUnlock/Defaults.ini
; on macOS. The log names the file it read. Write a value instead of default to change that
; setting for this game only.

[CameraUnlock]
; Written by the mod. Leave this section in place.
ConfigFormat=1

[Network]
; UDP port the mod receives tracker data on (OpenTrack protocol).
UdpPort=default

[General]
; true: head tracking is on when the game starts. ToggleKey turns it on and off.
EnableOnStartup=default
; true: yaw turns around the world's up axis. false: around the camera's own up axis.
WorldSpaceYaw=default
; true: turning your head turns the view.
; Tracking mode at startup, with PositionEnabled. The mode hotkey changes both.
RotationEnabled=default

[Smoothing]
; Smoothing when the tracker runs on this PC. 0 is the least, 1 the most.
LocalSmoothing=default
; Smoothing when the tracker is another device on the network, such as a phone.
; 0 is the least, 1 the most.
RemoteSmoothing=default

[Position]
; true: moving your head moves the view.
; Tracking mode at startup, with RotationEnabled. The mode hotkey changes both.
PositionEnabled=default
; How far, in metres, leaning left or right can move the view.
PositionLimitX=default
; How far, in metres, raising your head can move the view.
PositionLimitY=default
; How far, in metres, lowering your head can move the view.
PositionLimitYDown=default
; How far, in metres, leaning forward can move the view.
PositionLimitZ=default
; How far, in metres, leaning back can move the view.
PositionLimitZBack=default
; true: leaning stops at walls instead of moving the view through them.
CollisionEnabled=default
; How far the view is held off a wall when you lean into it, in centimetres. Keep it
; above 10, the camera's near clip, or the wall is cut away before the view reaches it.
CollisionMargin=15.0
; Which of the game's collision channels the wall check tests against. 0 is Visibility.
; CollisionChannel=0
; How gently the view eases back out after a wall stopped a lean.
; 0 is the quickest, 1 the slowest.
CollisionReleaseSmoothing=default

[Hotkeys]
; Turns head tracking on and off.
ToggleKey=default
; Changes the tracking mode: rotation and position, rotation only, position only.
CycleTrackingModeKey=default
; Switches yaw between the world's up axis and the camera's own (WorldSpaceYaw).
YawModeKey=default

[Camera]
; Degrees added to the game's field of view, -60 to 60. 0 leaves it as it is. The
; game has a Field of View slider in Options -> Graphics, and this adds to whatever
; you set there, so it can reach past that slider's range. The result is held
; between 40 and 150 degrees. The rendered view only: interaction, audio and
; streaming keep the game's own value.
FovOffset=0.0
; Which of the game's collision channels the aim ray runs on, to put the game's
; crosshair over what the aim is pointing at. 0 is Visibility.
; AimTraceChannel=0
```
<!-- /cameraunlock:config -->

### Field of view

The game has its own Field of View slider in Options -> Graphics. `FovOffset`
under `[Camera]` adds degrees to whatever that slider is set to, from -60 to
60, so it can reach past the slider's range; the result is held between 40 and
150 degrees, and 0 leaves the game's field of view exactly as it is. Only the
rendered view changes: interaction, audio and streaming keep the game's own
value.

### The crosshair

The game draws its own dot crosshair (Options -> Controls -> Display Dot
Crosshair). Head tracking moves the view off the direction you are pointing, so
the mod moves that crosshair onto the point your look and interaction ray
really hits. `AimTraceChannel` and `CollisionChannel` name collision channels of
the game itself, so the file holds them commented out at their defaults.

## Troubleshooting

**Mod not loading**

- Check that both `winmm.dll` and `EthanCarterReduxHeadTracking.asi` are in
  `<game>\EthanCarter\Binaries\Win64\`, next to
  `EthanCarter-Win64-Shipping.exe`.
- Look for `HeadTracking.log` in that same folder. It is rewritten on every
  launch, with the launch before it kept as `HeadTracking.prev.log`, so it is
  safe to attach to a bug report as-is.
- "Staying dormant" in the log means the game build did not match a known build
  profile. Open an issue with the log attached.

**No tracking response**

- Confirm your tracker is started and sending to `127.0.0.1:4242`.
- `udp: First UDP packet received` in `HeadTracking.log` is the line that
  confirms the tracker's data reached the game.
- Only one process can listen on port `4242` at a time. If another modded game
  is still open, the mod logs `Failed to bind UDP port 4242`. Close the other
  game and leave this one running: the mod retries every 500ms and picks the
  port up within about a second, logging `Bound UDP port 4242`.
- Check that tracking has not been toggled off with `End` or `Ctrl+Shift+Y`.

**Jittery or unstable tracking**

- Raise `RemoteSmoothing` if the tracker is a phone or another device on the
  network, or `LocalSmoothing` if it runs on this PC.
- A phone app sending a raw feed direct to the PC will jitter regardless. Route
  it through OpenTrack so its filters can clean the signal up first.
- For a webcam, more light and a plainer background both help more than any
  setting here.

**Wrong rotation axis, or the view leans when you turn**

- Press `Page Down` (or `Ctrl+Shift+H`) to switch between world-locked and
  camera-local yaw. World-locked is the default and keeps yaw about the
  horizon. Camera-local turns it about the camera's own up-axis, which leans
  the view once the camera is pitched up or down.
- If an axis moves the wrong way entirely, invert it in your tracker's own
  settings (OpenTrack's Output tab, or the phone app). The mod applies the pose
  as the tracker sends it.

**The game window moved when I launched**

- By design, and only when you play windowed: once the game has finished
  placing its window, the mod centers it on the work area of the monitor it
  opened on. A window the game centered itself, and a fullscreen or borderless
  one that already fills the screen, are left where they are. There is no
  setting for this.

## Updating

Download the new release and run `install.cmd` again. Your config is preserved.

## Uninstalling

Run `uninstall.cmd`. This removes the mod DLLs. The ASI loader is only removed
if the installer put it there. Use `uninstall.cmd /force` to remove it anyway.

## Building from Source

Requires [pixi](https://pixi.sh), CMake, and the Visual Studio 2022 build tools.

```powershell
git clone --recurse-submodules https://github.com/itsloopyo/the-vanishing-of-ethan-carter-redux-headtracking
cd the-vanishing-of-ethan-carter-redux-headtracking
pixi run build
pixi run package
```

Outputs land in `release/`.

## Community & Support

- [Discord](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch of head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your phone into a head tracker

## License

MIT License - see [LICENSE](LICENSE) for details.

Bundled and statically linked third-party components keep their own licenses,
reproduced in full in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

## Credits

- Game by [The Astronauts](https://store.steampowered.com/app/258520/).
- Loader: [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) by ThirteenAG.
- Protocol: [OpenTrack](https://github.com/opentrack/opentrack).
- Hooking: [MinHook](https://github.com/TsudaKageyu/minhook) by Tsuda Kageyu.
- Shared infrastructure: [cameraunlock-core](https://github.com/itsloopyo/cameraunlock-core).
