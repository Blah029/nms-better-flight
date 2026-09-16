#!/usr/bin/env python3
"""
Better Flight - control remap (data mod).

Reads controls.ini and rewrites the vanilla ship bindings so the inputs used by
BetterFlight.dll ([flight]) are free, moving displaced vanilla actions to what
[remap] lists. A [remap] entry may list several inputs; keyboard keys go into the
game's keyboard bindings file and MouseN into its mouse bindings file:

    Ship_RollLeft = KeyQ, Mouse4

    ./controls_mod.py             build + install
    ./controls_mod.py --check     show the resulting ship bindings + conflicts, change nothing
    ./controls_mod.py --build     compile only
    ./controls_mod.py --ini FILE  use another ini (testing)
    ./controls_mod.py --uninstall
"""
import argparse, configparser, re, shutil, sys
import xml.etree.ElementTree as ET
from copy import deepcopy
from pathlib import Path
import common as C

WORK     = C.WORK / "controls"
PAK      = C.PCBANKS / "NMSARC.MetadataEtc.pak"
MOD      = "BetterFlightControls"
FILES    = {"keyboard": "gcinputbindings_win_keyboard", "mouse": "gcinputbindings_win_mouse"}
LEGACY   = ["StrafeTest"]
HARMLESS = {"TextChatPasteHold"}          # only active while text chat is open
EQUIV    = {"LCtrl": "Ctrl", "RCtrl": "Ctrl", "LShift": "Shift", "RShift": "Shift", "LAlt": "Alt", "RAlt": "Alt"}


def dest(stem):
    return Path("METADATA") / "INPUT" / "BINDINGS" / f"{stem.upper()}.MBIN"


def get(node, name):
    for p in node:
        if p.get("name") == name:
            return p
    return None


def tokens(spec):
    return [t for t in re.split(r"[,\s]+", spec or "") if t]


def is_mouse(tok):
    return tok.lower().startswith("mouse")


def load_ini(path):
    cp = configparser.ConfigParser(inline_comment_prefixes=(";",), strict=True)
    cp.optionxform = str
    if not cp.read(path):
        sys.exit(f"missing {path}")
    return cp


def extract(stem):
    WORK.mkdir(parents=True, exist_ok=True)
    mbin = WORK / f"{stem}.mbin"
    tmp = WORK / "_x"
    shutil.rmtree(tmp, ignore_errors=True)
    C.extract(PAK, f"*{stem}*", tmp)
    src = next(tmp.rglob(f"{stem}.mbin"), None)
    if not src:
        sys.exit(f"extract failed: {stem}")
    shutil.copy(src, mbin)
    shutil.rmtree(tmp, ignore_errors=True)
    ok, why = C.roundtrip_clean(C.MBINC, mbin, WORK / "_rt")
    if not ok:
        sys.exit(f"ABORT: {stem} round-trip not lossless ({why}). Run ./setup_tools.py")
    mxml = mbin.with_suffix(".MXML")
    mxml.unlink(missing_ok=True)
    C.run([C.MBINC, "convert", "--overwrite", mbin])
    return mxml


def ship_container(tree):
    for s in list(tree.getroot())[0]:
        if get(get(s, "Type"), "ActionSetType").get("value") == "ShipControls":
            return get(s, "InputBindings")
    sys.exit("ShipControls set not found")


def act(b):  return get(get(b, "Action"), "InputAction").get("value")
def btn(b):  return get(get(b, "Button"), "InputButton").get("value")
def axis(b): return get(get(b, "Axis"), "InputAxis").get("value")


def make_row(template, action, button):
    n = deepcopy(template)
    get(get(n, "Action"), "InputAction").set("value", action)
    get(get(n, "Button"), "InputButton").set("value", button)
    get(get(n, "Axis"), "InputAxis").set("value", "None")
    return n


def rebind(cont, template, action, inputs):
    """Replace every button binding of `action` with `inputs`, keeping its position."""
    rows = [b for b in list(cont) if act(b) == action and axis(b) == "None"]
    old = [btn(b) for b in rows]
    kids = list(cont)
    pos = kids.index(rows[0]) if rows else len(kids)
    for b in rows:
        cont.remove(b)
    for j, inp in enumerate(inputs):
        cont.insert(pos + j, make_row(rows[0] if rows else template, action, inp))
    return old


def apply(trees, cp):
    conts = {k: ship_container(t) for k, t in trees.items()}
    tmpl  = {k: deepcopy(list(c)[0]) for k, c in conts.items()}
    remap = dict(cp["remap"])
    moved, unknown = [], []
    touched = {"keyboard": False, "mouse": False}

    for action, spec in remap.items():
        toks = tokens(spec)
        want = {"keyboard": [t for t in toks if not is_mouse(t)], "mouse": [t for t in toks if is_mouse(t)]}
        present = any(act(b) == action for c in conts.values() for b in c)
        if not present:
            unknown.append(action)
            continue
        old = []
        for kind in ("keyboard", "mouse"):
            had = any(act(b) == action and axis(b) == "None" for b in conts[kind])
            if had or want[kind]:
                old += rebind(conts[kind], tmpl[kind], action, want[kind])
                touched[kind] = True
        moved.append((action, "+".join(old) or "-", "+".join(toks) or "(unbound)"))

    for c in conts.values():
        for i, b in enumerate(list(c)):
            b.set("_index", str(i))

    def binds(kind):
        return [(act(b), btn(b)) for b in conts[kind] if axis(b) == "None"]

    def same(a, b):
        return a == b or EQUIV.get(a) == b

    conflicts, shared = [], []
    for name, spec in cp["flight"].items():
        for t in tokens(spec):
            for a, b in binds("mouse" if is_mouse(t) else "keyboard"):
                if same(t, b):
                    conflicts.append((name, t, a))
    for action, spec in remap.items():
        for t in tokens(spec):
            others = [a for a, b in binds("mouse" if is_mouse(t) else "keyboard")
                      if same(t, b) and a != action and t not in tokens(remap.get(a, ""))]
            if others:
                shared.append((action, t, sorted(set(others))))
    return conts, moved, unknown, conflicts, shared, touched


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--build", action="store_true", help="compile only, don't install")
    ap.add_argument("--ini", default=str(C.ROOT / "controls.ini"))
    ap.add_argument("--uninstall", action="store_true")
    a = ap.parse_args()

    if a.uninstall:
        for m in [MOD] + LEGACY:
            p = C.MODS / m
            if p.exists():
                shutil.rmtree(p); print(f"  removed {p}")
        return

    st = C.require_current_toolchain()
    cp = load_ini(a.ini)
    print(f"Better Flight controls  (toolchain {st['mbincompiler']}, ini {Path(a.ini).name})\n")
    mxml = {kind: extract(stem) for kind, stem in FILES.items()}
    trees = {kind: ET.parse(p) for kind, p in mxml.items()}
    conts, moved, unknown, conflicts, shared, touched = apply(trees, cp)

    print("  vanilla actions rebound:")
    for action, old, new in moved:
        print(f"    {action:24s} {old:>14s} -> {new}")
    for u in unknown:
        print(f"    !! {u}: not in this build's ShipControls - ignored")

    print("\n  flight inputs (BetterFlight.dll):")
    for k, v in cp["flight"].items():
        print(f"    {k:24s} {v}")

    soft = [c for c in conflicts if c[2] in HARMLESS]
    hard = [c for c in conflicts if c[2] not in HARMLESS]
    if soft:
        print("\n  harmless overlaps (only active while text chat is open):")
        for f, k, act_ in soft:
            print(f"    {k:8s} {f} / {act_}")
    if shared:
        print("\n  note - remapped input also used by another ship action:")
        for action, t, others in shared:
            print(f"    {t:8s} {action} + {', '.join(others)}")
    if hard:
        print("\n  !! CONFLICTS - these inputs would do two things while flying:")
        for f, k, act_ in hard:
            print(f"    {k:8s} {f}  vs vanilla {act_}")
        print("  Fix controls.ini before installing.")
        if not a.check:
            sys.exit(1)

    for kind in ("keyboard", "mouse"):
        print(f"\n  resulting ShipControls {kind} bindings:")
        for act_, b in sorted(((act(r), btn(r)) for r in conts[kind] if axis(r) == "None"), key=lambda r: r[1]):
            print(f"    {b:12s} {act_}")

    if a.check:
        return

    built = {}
    for kind, tree in trees.items():
        if not touched[kind]:
            continue                                       # ship only files we changed
        tree.write(mxml[kind], encoding="utf-8", xml_declaration=True)
        out = mxml[kind].with_suffix(".MBIN")
        out.unlink(missing_ok=True)
        r = C.run([C.MBINC, "convert", "--overwrite", mxml[kind]])
        if not out.exists():
            sys.exit(f"compile failed ({kind}):\n{r.stdout}")
        built[kind] = out

    if a.build:
        for kind, out in built.items():
            print(f"  built {kind} -> {out}")
        return

    for m in LEGACY:
        p = C.MODS / m
        if p.exists():
            shutil.rmtree(p); print(f"\n  removed superseded mod {m}")
    shutil.rmtree(C.MODS / MOD, ignore_errors=True)
    for kind, out in built.items():
        d = C.MODS / MOD / dest(FILES[kind])
        d.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(out, d)
        print(f"  installed {kind} bindings -> {d.relative_to(C.MODS)}")


if __name__ == "__main__":
    main()
