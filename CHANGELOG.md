# Changelog

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
