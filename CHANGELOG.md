# Changelog

## [0.0.0] - 2026-09-08

### Added

- Head tracking now moves the view by the same amount on screen whether you are
  walking or running. The game widens its field of view by six degrees while
  you run, and a wider view moves the picture less for the same turn of your
  head, so tracking used to go soft the moment you broke into a run and firm up
  again when you stopped. The mod reads the field of view the frame is actually
  being rendered at, compares it against the one you set in Options ->
  Graphics, and scales the head pose to match. Head tilt is left alone, because
  a tilt rotates the picture rather than sliding it. There is nothing to
  configure, and the correction is exactly nothing while the game is at your
  own setting.

- Windowed play now starts with the game window centred on the monitor it
  opened on. The mod waits for the game to finish placing the window, leaves a
  window the game already centred alone, and leaves fullscreen and borderless
  windows where they are. There is no setting for this.
- The game's crosshair now follows the point your look and interaction ray
  actually hits. Head tracking moves the view off the direction the mouse is
  pointing, which leaves the crosshair marking a spot you are no longer aimed
  at; the mod projects the real aim point into the frame being drawn and moves
  the game's own dot there, so it stays glued to the thing in front of you
  however you turn or lean your head. The mod moves the crosshair the game
  draws and never adds one of its own, so it needs the crosshair switched on in
  Options -> Controls -> Display Dot Crosshair to be visible at all.
  `[Reticle] MoveCrosshair=0` leaves it fixed at the centre of the frame.
- Leaning into a wall now stops at the wall. The mod sweeps the level from
  where the game put the camera toward where your head wants to go and cuts
  the lean to whatever the room leaves, holding the view far enough off a
  surface that it is still drawn, so a 40cm lean into a doorframe no longer
  puts the view inside it and shows you the corridor beyond. Turned off with
  `[Position] CollisionEnabled=0`; `CollisionRadius` sets how far off a
  surface the view is held and `CollisionReleaseSmoothing` how quickly the
  lean opens back up once you step clear.
- `[Camera] FovOffset`: degrees added to the game's field of view, on the
  rendered view only. It reads the value the frame is actually being drawn at,
  so it follows the Field of View slider in Options -> Graphics and any zoom the
  game does for itself, and it stacks on your setting rather than replacing it.
- Initial 6DOF head tracking for The Vanishing of Ethan Carter Redux (UE4),
  built as an Ultimate ASI Loader plugin that hooks the player view point in
  the render path only - look and aim stay decoupled.
- OpenTrack UDP receiver on port 4242, with head position as well as rotation,
  so you can lean in and peek around what the level puts in your way.
- Nav-cluster hotkeys (End/PageUp/PageDown) plus Ctrl+Shift+Y/G/H chords.
- PE-fingerprint build profile failsafe: the mod stays dormant on any build it
  does not recognise, so the game always runs vanilla on an unknown patch.
- `HeadTracking.log`, written beside the game exe and started fresh on every
  launch so it never grows across sessions. It stays short enough to attach to a
  bug report: the startup detail in full, then a line whenever the tracking state
  changes (tracker data stopped, a menu opened, a hotkey pressed) and at least
  one every five minutes so you can see it is still running.
- A single previous log generation: the launch before the current one is kept as
  `HeadTracking.prev.log`, so a crash the user only fetches the log for after
  relaunching is still diagnosable.
- The UDP port is reclaimed without a relaunch. If another game is still holding
  port 4242 when this one starts, the mod retries the bind every 500ms and
  starts listening within about a second of that game closing.

- Centring is done in your tracker rather than in the mod. There is no recenter
  key to press here: use opentrack's Center bind, or the CENTER button in your
  phone app, and the mod applies the pose it is sent exactly as it arrives. A
  centre on both sides sits in series with the tracker's own and the two drift
  apart, which shows up later as a view parked off to one side that takes two
  presses to put right.
- Two smoothing settings in `[Rotation]`, picked automatically per connection
  from where the packets come from. `LocalSmoothing` (default 0.0) applies to a
  tracker on this machine, `RemoteSmoothing` (default 0.15) to a phone or other
  device on the network, which has jitter to take the edge off. Both cover head
  rotation and head position.
- Every value in `HeadTracking.ini` is checked when it is read. Anything the mod
  cannot read as a number is refused and the shipped default used; a number
  outside a key's accepted range is pulled to the nearest end of it. Either way
  `HeadTracking.log` names the key and says what was used, so a setting that did
  not take effect is visible rather than silent. The generated file documents
  each key's accepted range next to it.
