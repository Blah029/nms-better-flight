# Changelog

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
