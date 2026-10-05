# BETTER FLIGHT

## THE FIRST TRUE 6DOF FLIGHT MOD FOR NO MAN'S SKY

**Strafe. Thrust up and down. Keep your momentum through a 180 and fly backwards at full speed.**

**Not just a speed tweak or a handling retune.** Real thrust on three axes the game never had, with a coupled / decoupled switch: vanilla handling plus strafe, or full Star Citizen-style 6DOF.

Every ship in No Man's Sky flies where its nose points. You can't slide sideways, you can't lift straight up, and you can never truly stop.

Better Flight adds the three directions the game never had — left/right, up/down and forward/back as real thrust — and lets you keep the speed and heading you built up while you point the ship wherever you like. Boost to full speed, flip around, and keep going that way until you thrust against it.

---

## Features

- **Strafe** left and right, **thrust** up and down
- **Coupled / decoupled flight** on **Z**
- **Real momentum:** the ship keeps flying the way you sent it instead of being pulled toward the nose
- Bind actions to several keys, including **mouse buttons**
- Settings editable while you play
- Works with ship speed and balance mods such as **Prepare To Sky**
- **F8** turns all mod features on and off

---

## Coupled and decoupled

**Coupled** *(default every launch)* — the game's own flight model, exactly as vanilla: turns, banking, braking, minimum speed. The only additions are the strafe and up/down thrusters. The game's flight assist works against them, so coupled strafe is a gentle push — raise `LateralAccel` / `VerticalAccel` in the settings if you want it stronger.

**Decoupled** — full 6DOF: nothing slows you down. Boost to top speed, let go, flip the ship around and keep flying backwards at full speed. Below about 30 m/s the game's normal flight takes over, so landing, take-off and hovering work as usual.

Pulse jump, landing and auto-follow are left to the game automatically in both modes. Walking around a corvette or on a spacewalk, the mod leaves the ship alone.

---

## Controls

| Key | Action | | Key | Action |
|---|---|---|---|---|
| **W / S** | thrust / brake | | **LShift** | boost |
| **A / D** | **strafe left / right** | | **B** | pulse jump *(was Space)* |
| **Space** | **thrust up** | | **N** | land *(was E)* |
| **LCtrl** | **thrust down** | | **F** | exit ship / tag / follow *(was E)* |
| **Q / E** | roll *(was A / D)* | | **C** | scan |
| **Z** | **coupled / decoupled** | | **F8** | mod on / off |
| **Mouse** | pitch / yaw | | | |

Pulse jump also fires if you hold **both** roll keys — that's built into the game, so it's now **Q + E**.

On a spacewalk, roll is **Shift + Q / Shift + E** — the game's own control, not shown in its settings.

---

## Installation

### Windows

1. Close No Man's Sky.
2. Open your No Man's Sky folder — the one containing `Binaries` and `GAMEDATA`.
   *(Steam: right-click the game → Manage → Browse local files.)*
3. Extract the zip **into that folder**, merging folders and overwriting files.
4. Launch the game normally.

**Updating from 1.2.x or earlier:** extract over the old version, then **delete `GAMEDATA/MODS/BetterFlight`** if it exists. The flight retune is built into the DLL now. (From 1.3.0 coupled mode is vanilla flight plus strafe — it no longer brakes the ship to a stop.)

**Manual install is recommended.** Part of the mod goes in the `Binaries` folder, which mod managers don't always handle.

### Linux / Steam Deck

Install as above, then do **one** of the following (only needed once):

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

- **Custom keybinds:** your saved keybinds in the game's Controls menu override this mod's layout, so roll / pulse jump / land / exit may not move. Reset controls to default, or set them to match the table above. If you moved **thrust, brake or boost** off W / S / LShift, set `ThrustKey`, `BrakeKey` and `BoostKey` in the settings file to match.
- **Game updates:** the mod usually keeps working after an update, and if it can't find what it needs in a new game version it switches that part off and the game runs normally. The key layout file is tied to a game version — if the game crashes or keys act strangely right after a patch, remove `GAMEDATA/MODS/BetterFlightControls` until an update is posted.
- **Keyboard and mouse only** for now — controllers and HOTAS can't strafe or switch modes yet.
- **No on-screen mode indicator** yet.
- **Text chat:** typing A, D, Space or Z in chat while flying also strafes or switches mode.
- **Multiplayer:** 1.2.1 stops ships interfering with each other and is still being tested. If anything odd happens near other players, set `Debug = 1` and attach `Binaries/BetterFlight.log` to your report.

---

## Settings

Edit `Binaries/BetterFlight.ini` — **with the game running** if you like; changes apply within about a second. Every setting is explained in the file.

| Setting | Default | What it does |
|---|---|---|
| `StrafeLeft` / `StrafeRight` / `StrafeUp` / `StrafeDown` | A / D / Space / LCtrl | strafe keys |
| `DecoupleKey` | Z | coupled / decoupled key |
| `ToggleKey` | F8 | all mod features on / off |
| `LateralAccel` / `VerticalAccel` | 70 / 55 | strafe thruster strength |
| `MaxStrafeSpeed` | 140 | strafe won't push past this speed |
| `ThrustKey` / `BrakeKey` / `BoostKey` | W / S / LShift | your throttle keys, as the mod sees them |
| `AutopilotKeys` | N, B, F | keys that hand control to the game (land, pulse, follow) |
| `WorldMomentum` | 1 | (decoupled only) 0 = the game's own momentum handling |
| `FlightRetune` | 1 | (decoupled only) 0 = the game's own handling tuning (F8 also toggles it) |
| `InvertLateral` / `InvertVertical` | 0 | set to 1 to flip a direction |
| `Debug` | 0 | set to 1 to write a log for bug reports |

**Mouse buttons and multiple keys:** separate inputs with commas, using `Mouse1`–`Mouse5`:
```
StrafeUp    = Space, Mouse5
DecoupleKey = KeyZ, Mouse4
```
Roll, pulse jump, land and exit are changed in the in-game Controls menu, which also accepts mouse buttons.

---

## Compatibility

**Works with** mods that change ship flight data — ship speed and balance mods such as Prepare To Sky. Better Flight's changes are applied on top of theirs.

**Conflicts with:**
- mods that replace the keyboard bindings file
- other mods installed as `Binaries/winmm.dll`

**Platforms:**
- Steam on Windows
- Steam on Linux / Steam Deck (Proton)
- GOG — untested
- Xbox / Game Pass — not supported

**Antivirus:** some antivirus tools flag unknown DLLs that hook into games. `winmm.dll` only loads inside No Man's Sky, and forwards calls to Windows' own winmm.dll.

---

## Bug reports

1. Set `Debug = 1` in `Binaries/BetterFlight.ini`.
2. Reproduce the problem.
3. Attach `Binaries/BetterFlight.log` to your report.

**Common fixes:**
- **Nothing happens (Linux):** check the launch option or run the helper — see Installation.
- **Nothing happens:** press **F8** in case the mod was switched off.
- **Ship brakes while holding the throttle:** set `ThrustKey` / `BrakeKey` / `BoostKey` to the keys you use.
- **A/D still roll:** the `BetterFlightControls` folder didn't install, or your custom keybinds are overriding it.
- **A/D suddenly roll after a game update or crash:** No Man's Sky switches *all* mods off after a crash, often caused by another out-of-date mod. Open `Binaries/SETTINGS/GCMODSETTINGS.MXML`, set `DisableAllMods` to `false`, and remove or update the mod that crashed.
- **Log says the old data mod is still installed:** delete `GAMEDATA/MODS/BetterFlight`.

---

## Uninstall

Delete:
- `Binaries/winmm.dll`
- `Binaries/BetterFlight.ini`
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
