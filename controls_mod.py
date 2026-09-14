#!/usr/bin/env python3
"""
Better Flight - control remap (data mod).

Reads controls.ini and rewrites the vanilla ShipControls keyboard bindings so the
keys claimed by BetterFlight.dll ([flight]) are free, with the displaced vanilla
actions moved to the keys in [remap].

    ./controls_mod.py            build + install
    ./controls_mod.py --check    show the resulting ship keymap + conflicts, change nothing
    ./controls_mod.py --uninstall
"""
import argparse, configparser, shutil, sys
import xml.etree.ElementTree as ET
from pathlib import Path
import common as C

INI    = C.ROOT / "controls.ini"
WORK   = C.WORK / "controls"
PAK    = C.PCBANKS / "NMSARC.MetadataEtc.pak"
MOD    = "BetterFlightControls"
STEM   = "gcinputbindings_win_keyboard"
DEST   = Path("METADATA") / "INPUT" / "BINDINGS" / f"{STEM.upper()}.MBIN"
LEGACY = ["StrafeTest"]          # superseded probe mods, removed on install


def get(node, name):
    for p in node:
        if p.get("name") == name:
            return p
    return None


def load_ini():
    cp = configparser.ConfigParser(inline_comment_prefixes=(";",))
    cp.optionxform = str                                   # keep case
    if not cp.read(INI):
        sys.exit(f"missing {INI}")
    return cp


def valid_keys():
    """TkInputEnum names, harvested from the decompiled bindings themselves."""
    return None                                            # filled lazily


def ship_set(root):
    for s in list(root)[0]:
        if get(get(s, "Type"), "ActionSetType").get("value") == "ShipControls":
            return s
    sys.exit("ShipControls set not found")


def keymap(ship):
    rows = []
    for b in get(ship, "InputBindings"):
        rows.append((get(get(b, "Action"), "InputAction").get("value"),
                     get(get(b, "Button"), "InputButton"),
                     get(get(b, "Axis"), "InputAxis").get("value")))
    return rows


def extract():
    WORK.mkdir(parents=True, exist_ok=True)
    mbin = WORK / f"{STEM}.mbin"
    tmp = WORK / "_x"
    shutil.rmtree(tmp, ignore_errors=True)
    C.extract(PAK, f"*{STEM}*", tmp)
    src = next(tmp.rglob(f"{STEM}.mbin"), None)
    if not src:
        sys.exit("extract failed")
    shutil.copy(src, mbin)
    shutil.rmtree(tmp, ignore_errors=True)
    ok, why = C.roundtrip_clean(C.MBINC, mbin, WORK / "_rt")
    if not ok:
        sys.exit(f"ABORT: bindings round-trip not lossless ({why}). Run ./setup_tools.py")
    mxml = mbin.with_suffix(".MXML")
    mxml.unlink(missing_ok=True)
    C.run([C.MBINC, "convert", "--overwrite", mbin])
    return mxml


def apply(mxml, cp):
    tree = ET.parse(mxml)
    ship = ship_set(tree.getroot())
    remap = dict(cp["remap"])
    flight = dict(cp["flight"])

    moved, unknown = [], []
    present = {a for a, _, _ in keymap(ship)}
    for action, newkey in remap.items():
        if action not in present:
            unknown.append(action)
            continue
        for a, btn, ax in keymap(ship):
            if a == action and ax == "None":
                old = btn.get("value")
                btn.set("value", newkey)
                moved.append((action, old, newkey))

    # conflicts: any flight key still bound to a vanilla ship action
    conflicts = []
    for fname, fkey in flight.items():
        for a, btn, ax in keymap(ship):
            if btn.get("value") == fkey or (fkey in ("LCtrl", "RCtrl") and btn.get("value") == "Ctrl"):
                conflicts.append((fname, fkey, a))
    return tree, ship, moved, unknown, conflicts


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--build", action="store_true", help="compile only, don't install")
    ap.add_argument("--uninstall", action="store_true")
    a = ap.parse_args()

    if a.uninstall:
        for m in [MOD] + LEGACY:
            p = C.MODS / m
            if p.exists():
                shutil.rmtree(p); print(f"  removed {p}")
        return

    st = C.require_current_toolchain()
    cp = load_ini()
    print(f"Better Flight controls  (toolchain {st['mbincompiler']})\n")
    mxml = extract()
    tree, ship, moved, unknown, conflicts = apply(mxml, cp)

    print("  vanilla actions moved:")
    for action, old, new in moved:
        print(f"    {action:24s} {old:>8s} -> {new}")
    for u in unknown:
        print(f"    !! {u}: not in this build's ShipControls - ignored")

    print("\n  flight keys (BetterFlight.dll):")
    for k, v in cp["flight"].items():
        print(f"    {k:24s} {v}")

    hard = [c for c in conflicts if c[2] not in ("TextChatPasteHold",)]
    soft = [c for c in conflicts if c[2] in ("TextChatPasteHold",)]
    if soft:
        print("\n  harmless overlaps (only active while text chat is open):")
        for f, k, act in soft:
            print(f"    {k:8s} {f} / {act}")
    if hard:
        print("\n  !! CONFLICTS - these keys would do two things while flying:")
        for f, k, act in hard:
            print(f"    {k:8s} {f}  vs vanilla {act}")
        print("  Fix controls.ini [remap] before installing.")
        if not a.check:
            sys.exit(1)

    print("\n  resulting ShipControls keyboard map:")
    for act, btn, ax in sorted(keymap(ship), key=lambda r: r[1].get("value")):
        if ax == "None":
            print(f"    {btn.get('value'):10s} {act}")

    if a.check:
        return

    tree.write(mxml, encoding="utf-8", xml_declaration=True)
    out = mxml.with_suffix(".MBIN")
    out.unlink(missing_ok=True)
    r = C.run([C.MBINC, "convert", "--overwrite", mxml])
    if not out.exists():
        sys.exit(f"compile failed:\n{r.stdout}")

    if a.build:
        print(f"  built -> {out}")
        return

    for m in LEGACY:
        p = C.MODS / m
        if p.exists():
            shutil.rmtree(p); print(f"\n  removed superseded mod {m}")
    dest = C.MODS / MOD / DEST
    shutil.rmtree(C.MODS / MOD, ignore_errors=True)
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy(out, dest)
    print(f"  installed -> {C.MODS / MOD}")


if __name__ == "__main__":
    main()
