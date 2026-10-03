===============================================================================
 BETTER FLIGHT {VERSION}  -  6DOF flight controls for No Man's Sky
 Built for No Man's Sky Steam build {BUILD}
===============================================================================

Strafe sideways, thrust up and down, and switch between coupled and decoupled
flight. Real momentum: the ship keeps flying the way you sent it instead of
being pulled toward the nose.


-------------------------------------------------------------------------------
 INSTALL / UPDATE - WINDOWS
-------------------------------------------------------------------------------
 1. Close No Man's Sky.
 2. Open your No Man's Sky folder - the one containing "Binaries" and
    "GAMEDATA". (Steam: right-click the game > Manage > Browse local files.)
 3. Extract this zip INTO that folder. Allow it to merge folders and
    overwrite files (when updating, this replaces the old version).
 4. Launch the game normally.

 UPDATING FROM 1.1.1 OR EARLIER: also delete the folder
 GAMEDATA/MODS/BetterFlight. The flight retune is built into winmm.dll now.
 If the old folder is still there, the mod skips its retune so nothing is
 applied twice.

 Manual install is recommended over mod managers: part of this mod goes in
 the Binaries folder, which mod managers don't always handle.


-------------------------------------------------------------------------------
 INSTALL - LINUX / STEAM DECK (Proton)
-------------------------------------------------------------------------------
 Do steps 1-3 above, then ONE of (not needed again when updating):

  A) Steam > No Man's Sky > Properties > Launch Options:
         WINEDLLOVERRIDES="winmm=n,b" %command%

  B) With the game closed, run once:
         python3 BetterFlight-docs/linux_wine_override.py

 Use exactly "n,b". Plain "n" stops the mod from working.


-------------------------------------------------------------------------------
 CONTROLS (keyboard + mouse)
-------------------------------------------------------------------------------
  W / S       thrust / brake           LShift   boost
  A / D       STRAFE left / right      B        pulse jump    (was Space)
  Space       THRUST UP                N        land          (was E)
  LCtrl       THRUST DOWN              F        exit / tag    (was E)
  Q / E       roll                     C        scan
  Z           COUPLED / DECOUPLED      F8       all mod features on/off
  Mouse       pitch / yaw

 Pulse jump also fires if you hold BOTH roll keys - that's built into the
 game, so it's now Q+E.

 On a spacewalk, roll is Shift+Q / Shift+E - the game's own control, not
 shown in its settings.


-------------------------------------------------------------------------------
 COUPLED AND DECOUPLED FLIGHT  (Z)
-------------------------------------------------------------------------------
 COUPLED (the default every time you launch)
   Thrusters hold you to what you command. Release W, boost and the strafe
   keys and the ship brakes to a full stop in every direction. No sideways
   slide when you turn.

 DECOUPLED
   Nothing slows you down. Boost to top speed, let go, flip the ship around
   and keep flying backwards at full speed. Your speed only changes when you
   thrust. Press Z again to re-couple and the ship brakes to a stop.

 Good to know:
  - Below about 30 m/s the game's own flight takes over, so landing, take-off
    and hovering work as usual.
  - Pulse jump, landing and auto-follow are left to the game automatically.
  - Walking around a corvette or on a spacewalk, the mod leaves the ship
    alone.
  - There is no on-screen indicator for the mode yet.


-------------------------------------------------------------------------------
 IF YOU HAVE CHANGED KEYBINDS IN THE GAME'S OPTIONS
-------------------------------------------------------------------------------
 Your saved keybinds override this mod's layout, so roll / pulse / land / exit
 may not move. Reset controls to default in Options > Controls, or set them by
 hand to match the table above.

 If you moved thrust, brake or boost off W / S / LShift, set ThrustKey,
 BrakeKey and BoostKey in BetterFlight.ini to match, or coupled mode will
 brake against you.


-------------------------------------------------------------------------------
 SETTINGS  -  Binaries/BetterFlight.ini
-------------------------------------------------------------------------------
 Edit it with the game running; changes apply within about a second. Every
 setting is explained in the file itself. The main ones:

  StrafeLeft etc.   strafe keys
  DecoupleKey       coupled/decoupled key                 (default Z)
  ToggleKey         all mod features on/off               (default F8)
  LateralAccel      strafe thruster strength              (default 70)
  VerticalAccel     up/down thruster strength             (default 55)
  MaxStrafeSpeed    strafe won't push past this           (default 140)
  RetroAccel        coupled braking of forward motion     (default 60)
  MainAccel         coupled braking of backward motion    (default 120)
  ThrustKey /       your throttle keys, used by coupled mode
  BrakeKey /
  BoostKey
  AutopilotKeys     keys that hand control to the game    (default N, B, F)
  WorldMomentum     0 = the game's own momentum handling  (default 1)
  FlightRetune      0 = the game's own handling tuning    (default 1)
  InvertLateral /   flip a direction
  InvertVertical
  Debug = 1         writes Binaries/BetterFlight.log  (for bug reports)

 MOUSE BUTTONS AND MULTIPLE KEYS: list several inputs separated by commas,
 using Mouse1 to Mouse5:
     StrafeUp    = Space, Mouse5
     DecoupleKey = KeyZ, Mouse4
 Mouse1 and Mouse2 are fire and zoom in the game, so side buttons work best.

 Changing Q/E/B/N/F here has no effect - use the in-game Controls menu, which
 also accepts mouse buttons.


-------------------------------------------------------------------------------
 UNINSTALL
-------------------------------------------------------------------------------
 Delete:
   Binaries/winmm.dll
   Binaries/BetterFlight.ini
   GAMEDATA/MODS/BetterFlightControls
 Linux: also remove the launch option, or run
   python3 linux_wine_override.py --remove


-------------------------------------------------------------------------------
 GAME UPDATES
-------------------------------------------------------------------------------
 No Man's Sky updates often. After an update:
  - Strafe, coupled/decoupled and the flight retune usually keep working. If
    the mod can't find what it needs in the new game version, it switches
    that part off and the game runs normally.
  - The control layout file is tied to a game version. If the game crashes or
    keys act strangely right after a game update, remove
    GAMEDATA/MODS/BetterFlightControls until an update is posted.


-------------------------------------------------------------------------------
 COMPATIBILITY
-------------------------------------------------------------------------------
 Works with mods that change ship flight data - ship speed and balance mods
 such as Prepare To Sky. Better Flight's changes are applied on top of theirs.

 Conflicts with:
  - mods that replace the keyboard bindings file
  - other mods installed as Binaries/winmm.dll

 Platforms: Steam on Windows and on Linux/Proton. GOG is untested. The Xbox /
 Game Pass version is not supported.


-------------------------------------------------------------------------------
 KNOWN LIMITATIONS
-------------------------------------------------------------------------------
  - Keyboard and mouse only: controllers and HOTAS can't strafe or switch
    modes yet.
  - Multiplayer: 1.2.1 stops ships interfering with each other, and is still
    being tested. If anything odd happens near other players, set Debug = 1
    and send Binaries/BetterFlight.log with your report.
  - No on-screen coupled/decoupled indicator.
  - Typing A, D, Space or Z in text chat while flying also strafes or
    switches mode.
  - Some antivirus tools flag unknown DLLs that hook games. winmm.dll only
    loads inside No Man's Sky and forwards calls to Windows' own winmm.dll.


-------------------------------------------------------------------------------
 TROUBLESHOOTING
-------------------------------------------------------------------------------
 Strafe / coupled mode does nothing:
  - Linux: check the launch option / override (step A or B above).
  - Press F8 in case the mod was switched off.
  - Set Debug = 1 in BetterFlight.ini, fly for a minute, and include
    Binaries/BetterFlight.log with your bug report.
 Ship brakes while you hold the throttle: set ThrustKey / BrakeKey / BoostKey
 to the keys you actually use.
 A/D still roll: the flight-controls folder didn't install, or your custom
 keybinds are overriding it (see above).
 A/D suddenly roll after a game update or crash: No Man's Sky switches ALL
 mods off after a crash, often caused by another mod that is out of date. Open
 Binaries/SETTINGS/GCMODSETTINGS.MXML, set DisableAllMods to false, and remove
 or update the mod that crashed.
 Log says the old data mod is still installed: delete
 GAMEDATA/MODS/BetterFlight.


-------------------------------------------------------------------------------
 CREDITS & LICENCE
-------------------------------------------------------------------------------
 Better Flight is MIT licensed - see LICENSE.txt.
 Uses MinHook, and signatures derived from NMS.py - see
 THIRD-PARTY-LICENSES.txt. No Man's Sky is (c) Hello Games; this is an
 unofficial fan mod.
