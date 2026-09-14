# Changelog

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
