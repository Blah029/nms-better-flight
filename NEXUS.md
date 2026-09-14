# Better Flight

**6DOF flight controls for No Man's Sky**

Strafe sideways. Thrust up and down. Stop dead in space. Turn without the ship banking on its own.

No Man's Sky ships only ever fly where the nose points. Better Flight gives you control over all six directions of movement — forward/back, left/right, up/down, pitch, yaw and roll — using a small native module that adds real thruster movement on top of a retuned flight model.

---

## Features

- **Strafe** left and right
- **Thrust** up and down
- Mouse turning no longer rolls the ship
- Ships can come to a complete stop
- Momentum carries — the ship doesn't snap to where the nose points
- Adjustable thruster strength, editable while you play
- **F8** toggles strafe on and off

---

## Controls

| Key | Action | | Key | Action |
|---|---|---|---|---|
| **W / S** | thrust / brake | | **LShift** | boost |
| **A / D** | **strafe left / right** | | **B** | pulse jump *(was Space)* |
| **Space** | **thrust up** | | **N** | land *(was E)* |
| **LCtrl** | **thrust down** | | **F** | exit ship / tag / follow *(was E)* |
| **Q / E** | roll *(was A / D)* | | **C** | scan |
| **Mouse** | pitch / yaw, no banking | | **F8** | strafe on / off |

Pulse jump also fires if you hold **both** roll keys — that's built into the game, so it's now **Q + E**.

---

## Installation

### Windows

1. Close No Man's Sky.
2. Open your No Man's Sky folder — the one containing `Binaries` and `GAMEDATA`.
   *(Steam: right-click the game → Manage → Browse local files.)*
3. Extract the zip **into that folder** and allow it to merge folders.
4. Launch the game normally.

**Manual install is recommended.** Part of the mod goes in the `Binaries` folder, which mod managers don't always handle.

### Linux / Steam Deck

Install as above, then do **one** of the following:

- **Launch option** — Steam → No Man's Sky → Properties → Launch Options:
  ```
  WINEDLLOVERRIDES="winmm=n,b" %command%
  ```
- **Or** close the game and run the included helper once:
  ```
  python3 BetterFlight-docs/linux_wine_override.py
  ```

Use exactly **`n,b`**. Plain `n` stops the mod from working.

---

## Please read before installing

- **Custom keybinds:** if you've changed keys in the game's Controls menu, your saved keybinds override this mod's layout, so roll / pulse jump / land / exit may not move. Reset controls to default, or set them by hand to match the table above. Strafe and thrust up/down work either way.
- **Game updates:** No Man's Sky updates often. Strafe usually keeps working after an update, and if it can't find what it needs in a new game version it switches itself off and the game runs normally. The flight retune is tied to a game version and may need a mod update after a patch — if flight feels wrong right after a game update, remove `GAMEDATA/MODS/BetterFlight` until an update is posted.
- **Keyboard only for now:** controllers and HOTAS can't strafe yet.
- **Text chat:** typing A, D or Space in text chat while flying also strafes.

---

## Settings

Edit `Binaries/BetterFlight.ini` — you can do this **with the game running**; changes apply within about a second.

| Setting | Default | What it does |
|---|---|---|
| `LateralAccel` | 70 | strafe thruster strength |
| `VerticalAccel` | 55 | up/down thruster strength |
| `MaxStrafeSpeed` | 140 | strafe won't push that direction past this speed |
| `StrafeLeft` / `StrafeRight` / `StrafeUp` / `StrafeDown` | A / D / Space / LCtrl | change the strafe keys |
| `ToggleKey` | F8 | strafe on/off key |
| `InvertLateral` / `InvertVertical` | 0 | set to 1 to flip a direction |
| `Debug` | 0 | set to 1 to write a log for bug reports |

To change roll, pulse jump, land or exit keys, use the in-game Controls menu.

---

## Compatibility

**Conflicts with:**
- other mods that replace `GCSPACESHIPGLOBALS.GLOBAL.MBIN` (most flight and ship-speed mods)
- mods that replace the keyboard bindings file
- other mods installed as `Binaries/winmm.dll`

**Platforms:**
- Steam on Linux / Steam Deck (Proton) — tested
- Steam on Windows — expected to work, reports welcome
- GOG — untested
- Xbox / Game Pass — not supported

**Antivirus:** some antivirus tools flag unknown DLLs that hook into games. `winmm.dll` only loads inside No Man's Sky, and forwards calls to Windows' own winmm.dll.

---

## Bug reports

1. Set `Debug = 1` in `Binaries/BetterFlight.ini`.
2. Reproduce the problem.
3. Attach `Binaries/BetterFlight.log` to your report.

The log records whether the module loaded and, if strafe didn't apply, why.

**Common fixes:**
- **Strafe does nothing (Linux):** check the launch option or run the helper — see Installation.
- **A/D still roll:** the `BetterFlightControls` folder didn't install, or your custom keybinds are overriding it — see *Please read before installing*.

---

## Uninstall

Delete:
- `Binaries/winmm.dll`
- `Binaries/BetterFlight.ini`
- `GAMEDATA/MODS/BetterFlight`
- `GAMEDATA/MODS/BetterFlightControls`

On Linux, also remove the launch option, or run:
```
python3 linux_wine_override.py --remove
```

---

## Credits

- [NMS.py](https://github.com/monkeyman192/NMS.py) by monkeyman192 — reverse-engineering data the function signatures are derived from
- [MinHook](https://github.com/TsudaKageyu/minhook) by Tsuda Kageyu — function hooking
- [MBINCompiler](https://github.com/monkeyman192/MBINCompiler) and [HGPAKtool](https://github.com/monkeyman192/HGPAKtool) — game data tooling

Developed with AI assistance (Anthropic's Claude) and tested in-game.

No Man's Sky is © Hello Games. This is an unofficial fan mod.
