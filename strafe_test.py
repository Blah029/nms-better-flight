#!/usr/bin/env python3
"""
Phase 1 — Strafe probe.

No Man's Sky defines seven lateral/vertical ship input actions that NO shipped
control profile binds (verified across all 9 profiles: keyboard, mouse, Xbox,
DS4, PS5, Switch, and every VR profile):

    144 ShipStrafe            (analogue 2D)
    145 ShipStrafeHorizontal  (analogue 1D)
    146 ShipStrafeVertical    (analogue 1D)
    169 ShipUp  170 ShipDown  171 ShipLeft  172 ShipRight   (digital)

They carry full loc tags (CTRL_ANLG_SHIP_STRAFE) and Steam Input external IDs
(ship_strafe / ship_strafeh / ship_strafev), so the game at least registers them.
Whether the flight code still *consumes* them is the open question, and it can
only be answered empirically.

This builds a mod that binds them so you can find out.

    ./strafe_test.py           # keyboard probe
    ./strafe_test.py --pad     # also remap pad right stick (sacrifices camera look)
    ./strafe_test.py --uninstall
"""
import argparse, shutil, sys
import xml.etree.ElementTree as ET
from copy import deepcopy
from pathlib import Path
import common as C

WORK = C.WORK / "strafe"
PAK  = C.PCBANKS / "NMSARC.MetadataEtc.pak"
MOD  = "StrafeTest"

# --------------------------------------------------------------------------
# CONTROL binding. Ship_Scan is normally KeyC; we ALSO bind it to KeyZ.
# If pressing Z fires the scanner, the modified binding file is definitely live
# and added bindings definitely work. Without this, a null result is worthless -
# you cannot tell "action is dead" from "my mod never loaded".
# --------------------------------------------------------------------------
CONTROL = [
    ("Ship_Scan",             "KeyZ",   "None"),
]

# TEST bindings. Arrow keys and Q/R are unused in the vanilla ShipControls set.
# NOTE: A and D are vanilla Ship_RollLeft / Ship_RollRight and are NOT touched.
KEYBOARD_PROBE = CONTROL + [
    ("ShipLeft",              "Left",   "None"),
    ("ShipRight",             "Right",  "None"),
    ("ShipUp",                "Up",     "None"),
    ("ShipDown",              "Down",   "None"),
    # Analogue actions on digital keys - the input layer may drop these even if
    # the flight code would consume them, so a null result here is weak.
    ("ShipStrafeHorizontal",  "KeyQ",   "None"),
    ("ShipStrafeVertical",    "KeyR",   "None"),
]

# The strong probe: real analogue axes. Costs you right-stick camera look.
PAD_PROBE = CONTROL + [
    ("ShipStrafeHorizontal",  "None",   "RightStickX"),
    ("ShipStrafeVertical",    "None",   "RightStickY"),
]

FILES = {
    "keyboard": "gcinputbindings_win_keyboard",
    "pad":      "gcinputbindings_xboxone_pad",
}


def get(node, name):
    for p in node:
        if p.get("name") == name:
            return p
    return None


def extract_and_decompile(stem):
    WORK.mkdir(parents=True, exist_ok=True)
    mbin = WORK / f"{stem}.mbin"
    if not mbin.exists():
        tmp = WORK / "_x"
        C.extract(PAK, f"*{stem}*", tmp)
        src = next(tmp.rglob(f"{stem}.mbin"), None)
        if not src:
            sys.exit(f"could not extract {stem}")
        shutil.copy(src, mbin)
        shutil.rmtree(tmp, ignore_errors=True)
    mxml = WORK / f"{stem}.MXML"
    mxml.unlink(missing_ok=True)
    r = C.run([C.MBINC, "convert", "--overwrite", mbin])
    if "WARN" in r.stdout or not mxml.exists():
        sys.exit(f"decompile failed for {stem}:\n{r.stdout}")
    return mxml


def add_bindings(mxml, probe):
    tree = ET.parse(mxml)
    root = tree.getroot()
    sets = list(root)[0]

    ship = None
    for s in sets:
        if get(get(s, "Type"), "ActionSetType").get("value") == "ShipControls":
            ship = s
            break
    if ship is None:
        sys.exit("no ShipControls set found")

    container = get(ship, "InputBindings")
    existing = list(container)
    template = deepcopy(existing[0])          # clone a known-valid binding node

    added = []
    idx = len(existing)
    for action, button, axis in probe:
        node = deepcopy(template)
        node.set("_index", str(idx))
        get(get(node, "Action"), "InputAction").set("value", action)
        get(get(node, "Button"), "InputButton").set("value", button)
        get(get(node, "Axis"), "InputAxis").set("value", axis)
        container.append(node)
        added.append((action, button, axis))
        idx += 1

    tree.write(mxml, encoding="utf-8", xml_declaration=True)
    return added


def compile_and_stage(mxml, stem, staging):
    out_mbin = mxml.with_suffix(".MBIN")
    out_mbin.unlink(missing_ok=True)
    r = C.run([C.MBINC, "convert", "--overwrite", mxml])
    if not out_mbin.exists():
        sys.exit(f"compile failed:\n{r.stdout}\n{r.stderr}")
    dest = staging / "METADATA" / "INPUT" / "BINDINGS" / f"{stem.upper()}.MBIN"
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy(out_mbin, dest)
    return dest


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pad", action="store_true",
                    help="also bind pad right stick to strafe (loses camera look)")
    ap.add_argument("--uninstall", action="store_true")
    a = ap.parse_args()

    dest_root = C.MODS / MOD
    if a.uninstall:
        if dest_root.exists():
            shutil.rmtree(dest_root); print(f"  removed {dest_root}")
        else:
            print("  not installed")
        return

    st = C.require_current_toolchain()
    print(f"Strafe probe — building  (toolchain {st['mbincompiler']})\n")
    staging = WORK / "staging"
    shutil.rmtree(staging, ignore_errors=True)

    targets = [("keyboard", KEYBOARD_PROBE)]
    if a.pad:
        targets.append(("pad", PAD_PROBE))

    for key, probe in targets:
        stem = FILES[key]
        mxml = extract_and_decompile(stem)
        added = add_bindings(mxml, probe)
        out = compile_and_stage(mxml, stem, staging)
        print(f"  {key}: +{len(added)} bindings -> {out.relative_to(staging)}")
        for act, btn, ax in added:
            src = f"axis={ax}" if ax != "None" else f"key={btn}"
            print(f"      {act:24s} {src}")

    shutil.rmtree(dest_root, ignore_errors=True)
    shutil.copytree(staging, dest_root)
    print(f"\n  installed -> {dest_root}")
    print("""
  ============================ HOW TO TEST ============================

  STEP 1 - CONTROL.  Get in a ship and press  Z.
      Z should trigger the SCANNER (normally C).

      Z does nothing  -> the mod is not loading. Stop here; the rest of the
                         test tells you nothing. Report this back.
      Z scans         -> added bindings work. Continue to step 2, and a null
                         result below is now real evidence.

  STEP 2 - STRAFE.  Fly to open space and come to a full stop, then press:

      Left / Right arrow   ShipLeft  / ShipRight            (digital)
      Up / Down arrow      ShipUp    / ShipDown             (digital)
      Q / R                Horizontal / Vertical Thrust     (analogue on a key)

      Watch for the ship translating sideways or up/down WITHOUT rotating.

  STEP 3 - UI.  Options -> Controls, look for "Horizontal Thrust" and
      "Vertical Thrust" in the ship section. If they are listed, the actions
      are live in the input layer even if the flight code ignores them.

  NOTE: A and D are vanilla ROLL and were never rebound - pressing them tells
  us nothing about strafe.

  =====================================================================

  Revert:  ./strafe_test.py --uninstall
""")


if __name__ == "__main__":
    main()
