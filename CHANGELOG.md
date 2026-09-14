# Changelog

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
