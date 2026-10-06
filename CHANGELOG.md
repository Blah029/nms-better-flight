# Changelog

## 1.3.6 — 2026-10-07

- **Coupled strafe now reaches `MaxStrafeSpeed`.** While a strafe key is
  held, the part of the game's lateral change that fights the active thrust
  is cancelled (the 1.3.5 velocity-relative rule, extended from the nose
  axis to the strafe axes), so the thruster runs unopposed up to the
  `MaxStrafeSpeed` cap - decoupled authority. Same-direction game pushes
  (e.g. the vanilla brake assisting a reversal) still pass through. On key
  release the cancellation drops away immediately and the vanilla bleed
  resumes, so the drift decays on its own with no forward kick (the 1.3.2
  window keeps the nose-axis rule armed as before). New simulator tests
  S8/S8' pin the cap and the release decay.

## 1.3.5 — 2026-10-07

- **The coupled strafe/switch window now cancels only the nose-axis push
  that amplifies the current nose-direction motion.** A game push opposing
  the nose velocity is damping (throttle wind-down, alignment) and passes
  through, so the game's own deceleration keeps working inside the window
  instead of the nose speed being frozen for up to 5 s. The unwanted
  conversion is still fully cancelled: a push can leak only at/against a
  zero crossing, where it creates motion in its own direction and then gets
  blocked - residual bounded to ~one frame's push. New simulator test S7
  pins the wind-down pass-through.

## 1.3.4 — 2026-10-06

- **Merged 1.2.1 into the 1.3.x line.** Per-ship flight state (an unpiloted
  corvette can no longer clobber your ship's state, one key press acts once
  however many ships the game updates), 150 ms key-press debounce, the ship
  census log, and controller diagnostics now ride on top of the 1.3.x
  vanilla-coupled + strafe design.
- The 1.3.x strafe-release / mode-switch window is per-ship state, like the
  rest of the flight memory.
- Simulator: the two-ship tests (M1-M5) run against 1.3.x behaviour - M3
  verifies a coupled scene leaves both ships alone (no mod braking, no
  velocity crossing between ships).

## 1.3.3 — 2026-10-05

- **Fix: switching to coupled with large off-nose velocity no longer kicks
  the ship forward/backwards.** Decoupled momentum flight can leave the ship
  drifting sideways at near-max speed with the nose 90° off; handing that
  state to the vanilla flight model made it react violently (live log:
  1629 m/s sideways → 448 m/s in one second, ~1200 m/s² of lateral brake and
  a ~440 m/s² nose push driving −197 m/s of backwards velocity). The decouple
  key now arms the same 5 s release window used for strafe release when the
  switch happens with > 5 m/s of off-nose drift: the game's nose-axis change
  is cancelled, and the drift bleeds off at the game's own (vanilla) rate —
  in the live case that was ~1.3 s. The lateral collapse itself is the
  vanilla `DirectionBrake` acting on a state that can't exist without the
  mod; staying decoupled keeps the drift. Switches with ≤ 5 m/s of drift, any
  throttle input, and all hand-off cases are unchanged.
- Simulator: new S5 (coupled switch with 1600 m/s sideways) and S6 (coupled
  switch, no drift) tests.

## 1.3.2 — 2026-10-04

- **Fix: releasing a coupled strafe in space no longer kicks the ship
  forward/backwards.** The game's velocity-toward-nose steering keeps
  converting the *residual* drift into forward speed after the key is
  released; 1.3.1 only cancelled it while the key was held. The nose-axis
  cancellation now stays armed for 5 s after the last strafe frame (it
  disarms early as soon as the drift bleeds below 5 m/s — the drift is gone in
  ~2–3 s), so a release lets the drift die sideways with no forward kick. The
  window is anchored to strafe activity, so vanilla slide-turn recovery (no
  recent strafe) is untouched, and the take-off/landing hand-off rule is
  suspended only for the window (there the low-speed side push is the known
  drift bleed, not the game driving the ship). No new exposure to game events:
  station/outpost approaches end in the N/B hand-off keys, impacts hand off,
  and any throttle input disarms the gate.
- Simulator: new S3 (release kick) and S4 (slide-turn protection) tests.

## 1.3.1 — 2026-10-04

- **Fix: coupled strafe in space no longer gains forward/backwards speed.**
  The game's flight model steers the velocity vector toward the nose in
  response to sustained sideways/vertical drift (live logs: a pure strafe in
  space drew up to ~30 m/s² of nose-axis push from the game, with the
  controller's overshoot producing forward *and* backwards phases). 1.2.x's
  coupled mode masked this by braking the uncommanded forward axis to zero
  every frame; 1.3.0's coupled mode (no mod braking, no momentum discard)
  let the conversion run unchecked. While coupled and strafing without
  forward input, the mod now cancels only the game's nose-axis velocity
  change: the drift builds and bleeds exactly as before (the "gentle push"
  feel is unchanged) and it stays sideways. It needs real drift (> 5 m/s), so
  hover/landing lift (nose-axis push, ~no drift) is untouched, and hand-off
  (autopilot, pulse drive, impacts, take-off) is unchanged. The bug was
  space-only in testing (corvette, in both retune states) and never affected
  decoupled mode — its momentum discard already cancels the same push.
- Docs: corrected the retune count — 1.3.0's table is 152 values (24
  always-scope + 128 decoupled-scope); 176 was the 1.2.x total.
- Simulator: new S / S' / S'' tests cover the conversion, a straight nose-axis
  push while strafing, and the lift safety case.

## 1.3.0 — 2026-10-02

- **Coupled mode is now vanilla flight plus strafe.** The mod's own coupled
  braking is gone: no more braking to a stop, no `RetroAccel` / `MainAccel`. In
  coupled mode the game's flight model runs exactly as loaded — turns, banking,
  braking, minimum speed, damping — and the only thing added on top is the
  strafe / up-down thrust. The game's flight assist works against it, so
  coupled strafe is a gentle push; raise `LateralAccel` / `VerticalAccel` in
  `BetterFlight.ini` (re-read live) for more.
- **The flight retune is now split by scope**: 24 values (space
  `MinSpeed` / `MinSpeedForce`) apply in both modes, the other 128 apply in
  decoupled mode only. Toggling to coupled restores the game's loaded values
  for those 128 fields; toggling back re-applies them on top.
  `FlightRetune = 0` leaves the flight data untouched in both modes.
- **Space minimum speed is 0 in both modes** (`MinSpeed` / `MinSpeedForce` on
  the space engines, 24 values, applied always) — the ship can come fully to
  rest in space even in coupled mode. Atmospheric minimum speed follows the
  mode: vanilla in coupled, 0 in decoupled.
- **World momentum is decoupled-mode only.** In coupled mode the game's own
  handling of velocity (steering toward the nose, drift, throttle wind-down)
  runs untouched; `WorldMomentum` / `MomentumMinSpeed` no longer apply there.
- `DriftSpeedCap` still applies in both modes (safety net).
- Simulator: the coupled braking tests (C, F', I, I', S, U'', K, Y') were
  reworked to verify the new behaviour (no mod writes, game model untouched),
  and new tests cover the mode-toggled retune scope and coupled strafe against
  the vanilla assist.

## 1.2.1 — 2026-10-03 (pre-release)

- **Fixed: ships interfering with each other.** The game runs the flight update
  for more than one ship per frame (corvettes with nobody aboard, and likely other
  players' ships in multiplayer), but the mod kept one global copy of "the ship's"
  state. One ship's velocity could be written into another ("sent in random
  directions"), an unpiloted ship marked Z/F8 as held every frame ("stuck in
  coupled mode"), and coupled braking stopped engaging. State is now per ship,
  and one key press acts once however many ships the game updates.
- Logs which ships go through the flight update and what controls them, and notes
  when two ships have a pilot in the same frame, for multiplayer reports.
- Controller diagnostics (`[input] Diagnostics`, off by default) for upcoming
  controller and HOTAS support. Read-only; logs nothing while off.
- Docs: what to do when A/D suddenly roll (the game switched mods off after a crash).
- Verified on Steam build 25624745.

## 1.2.0 — 2026-09-15

- **Flight retune moved into the DLL.** Up to 1.1.1 it shipped as a replacement
  `GCSPACESHIPGLOBALS.GLOBAL.MBIN`, which conflicted with every mod touching that
  file. `winmm.dll` now finds the same 176 flight values by name, through the
  game's own reflection metadata, and applies them in memory on top of whatever the
  game loaded. Tested with *Prepare To Sky*: PTSd's values load, Better Flight's
  changes go on top, and PTSd's other 103 ship changes stay as they are.
- **Updating: delete `GAMEDATA/MODS/BetterFlight`.** While it is present the DLL
  skips the retune (and logs why), so nothing is applied twice.
- New `FlightRetune` setting (default 1). `0`, or F8, puts the loaded values back.
- If the game reloads its flight data, the retune is applied again on the new values.
- Fields a future game version renames are skipped and logged; the rest still apply.

## 1.1.1 — 2026-09-15

- **Fixed: a corvette could be flown from outside the pilot seat.** On a spacewalk
  (or walking around inside), strafe and coupled braking still moved the corvette, so
  jetpack keys sent it flying off. The game keeps simulating corvettes with nobody at
  the controls; Better Flight now acts only when the game reports a pilot at the
  controls, and reads no keys otherwise.
- Rebuilt for Steam build 25320008 (Cosmos 7.02). The control layout is generated
  from the updated default bindings, so the on-foot alt-weapon key stays on B.
- Docs: on a spacewalk the game's roll is Shift+Q / Shift+E (not shown in its
  settings).

## 1.1.0 — 2026-09-14

- **Coupled / decoupled flight** (Z, default coupled at every launch).
  Coupled: the ship holds zero velocity on every axis you aren't commanding, so
  releasing the throttle brakes to a full stop along the direction of travel
  (`RetroAccel` for forward motion, `MainAccel` for backward). Decoupled: no
  braking at all — speed and direction are kept through turns and flips, e.g.
  boost to top speed, flip 180° and keep flying backwards.
- **Real momentum** (`WorldMomentum`): above `MomentumMinSpeed` (30 m/s) the
  game's pull toward the nose, drift correction and throttle wind-down are
  ignored. Below it the game handles landing, take-off and hover as normal.
- The mod hands control to the game during pulse jump, landing and auto-follow
  (`AutopilotKeys`, plus detection of pulse spool-up and hard impacts).
- **Multiple inputs per action, including mouse buttons** (`Mouse1`–`Mouse5`),
  for the strafe/mode keys in `BetterFlight.ini` and for the relocated vanilla
  actions.
- F8 now switches every Better Flight native feature on/off.
- **Fixed: ships couldn't stop or reverse in atmosphere** — they kept creeping
  forward. Atmospheric minimum speed is now 0.
- Drift safety cap (`DriftSpeedCap`) applies only to sideways + vertical speed.

## 1.0.1 — 2026-09-14

- **Fixed: game failed to launch on Windows** with *"The procedure entry point
  waveOutReset could not be located…"*. Better Flight's `winmm.dll` only
  provided the two functions No Man's Sky itself uses, but other DLLs loaded into
  the game — Steam's client, overlays, frame generation — use more of winmm and
  Windows refuses to start if any are missing. It now forwards the complete winmm
  interface (every export of Windows 10/11's winmm.dll, plus Wine's). Linux was
  unaffected: Wine tolerates missing imports.
- Builds are reproducible, so a VirusTotal link stays valid when re-packaging.

## 1.0.0 — 2026-09-13

First public release. Built for Steam build 25233815.

- **Strafe** left/right (A/D) and **thrust up/down** (Space/LCtrl), added by a
  native module that finds what it needs in the game's code at startup.
- **Flight retune**: turning with the mouse no longer banks the ship, ships can
  come to a full stop, and momentum carries instead of snapping to the nose.
- **Controls moved** off the new flight keys: roll Q/E, pulse jump B, land N,
  exit/tag/follow F. F8 toggles strafe.
- `Binaries/BetterFlight.ini`: keys and thruster strength, re-read while playing.
- Linux/Steam Deck: optional helper that sets the Proton override for you.
