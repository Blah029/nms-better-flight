===============================================================================
 BETTER FLIGHT {VERSION}  -  6DOF flight controls for No Man's Sky
 Built for No Man's Sky Steam build {BUILD}
===============================================================================

Strafe sideways, thrust up and down, stop dead in space, and turn without the
ship banking on its own. All six directions of movement are now under your
control: forward/back, left/right, up/down, pitch, yaw and roll.


-------------------------------------------------------------------------------
 INSTALL - WINDOWS
-------------------------------------------------------------------------------
 1. Close No Man's Sky.
 2. Open your No Man's Sky folder - the one containing "Binaries" and
    "GAMEDATA". (Steam: right-click the game > Manage > Browse local files.)
 3. Extract this zip INTO that folder. Allow it to merge folders.
 4. Launch the game normally.

 Manual install is recommended over mod managers: part of this mod goes in
 the Binaries folder, which mod managers don't always handle.


-------------------------------------------------------------------------------
 INSTALL - LINUX / STEAM DECK (Proton)
-------------------------------------------------------------------------------
 Do steps 1-3 above, then ONE of:

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
  Mouse       pitch / yaw, no banking  F8       strafe on/off

 Pulse jump also fires if you hold BOTH roll keys - that's built into the
 game, so it's now Q+E.


-------------------------------------------------------------------------------
 IF YOU HAVE CHANGED KEYBINDS IN THE GAME'S OPTIONS
-------------------------------------------------------------------------------
 Your saved keybinds override this mod's layout, so roll / pulse / land / exit
 may not move. Reset controls to default in Options > Controls, or set them by
 hand to match the table above. Strafe and thrust up/down work either way.


-------------------------------------------------------------------------------
 SETTINGS  -  Binaries/BetterFlight.ini
-------------------------------------------------------------------------------
 Edit it with the game running; changes apply within about a second.

  LateralAccel     strafe thruster strength         (default 70)
  VerticalAccel    up/down thruster strength        (default 55)
  MaxStrafeSpeed   strafe won't push past this      (default 140)
  StrafeLeft etc.  change the strafe keys
  InvertLateral /  flip a direction
  InvertVertical
  Debug = 1        writes Binaries/BetterFlight.log  (for bug reports)

 Changing Q/E/B/N/F here has no effect - use the in-game Controls menu.


-------------------------------------------------------------------------------
 UNINSTALL
-------------------------------------------------------------------------------
 Delete:
   Binaries/winmm.dll
   Binaries/BetterFlight.ini
   GAMEDATA/MODS/BetterFlight
   GAMEDATA/MODS/BetterFlightControls
 Linux: also remove the launch option, or run
   python3 linux_wine_override.py --remove


-------------------------------------------------------------------------------
 GAME UPDATES
-------------------------------------------------------------------------------
 No Man's Sky updates often. After an update:
  - Strafe usually keeps working. If it can't find what it needs in the new
    game version, it switches itself off and the game runs normally.
  - The flight retune and control layout are tied to a game version and may
    need a new release of this mod. If flight feels wrong right after a game
    update, remove GAMEDATA/MODS/BetterFlight until an update is posted.


-------------------------------------------------------------------------------
 COMPATIBILITY
-------------------------------------------------------------------------------
 Conflicts with:
  - other mods that replace GCSPACESHIPGLOBALS.GLOBAL.MBIN (most flight and
    ship-speed mods)
  - mods that replace the keyboard bindings file
  - other mods installed as Binaries/winmm.dll

 Platforms: tested on Steam under Linux/Proton. Steam on Windows is expected to
 work. GOG is untested. The Xbox / Game Pass version is not supported.


-------------------------------------------------------------------------------
 KNOWN LIMITATIONS
-------------------------------------------------------------------------------
  - Keyboard only: controllers and HOTAS cannot strafe yet.
  - Typing A, D or Space in text chat while flying also strafes.
  - Some antivirus tools flag unknown DLLs that hook games. winmm.dll only
    loads inside No Man's Sky and forwards calls to Windows' own winmm.dll.


-------------------------------------------------------------------------------
 TROUBLESHOOTING
-------------------------------------------------------------------------------
 Strafe does nothing:
  - Linux: check the launch option / override (step A or B above).
  - Set Debug = 1 in BetterFlight.ini, fly for a minute, and include
    Binaries/BetterFlight.log with your bug report.
 A/D still roll: the flight-controls folder didn't install, or your custom
 keybinds are overriding it (see above).


-------------------------------------------------------------------------------
 CREDITS & LICENCE
-------------------------------------------------------------------------------
 Better Flight is MIT licensed - see LICENSE.txt.
 Uses MinHook, and signatures derived from NMS.py - see
 THIRD-PARTY-LICENSES.txt. No Man's Sky is (c) Hello Games; this is an
 unofficial fan mod.
