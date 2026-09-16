#!/usr/bin/env python3
"""
Better Flight — No Man's Sky flight model overhaul (Phase 0: data-only)

Rebuilds GCSPACESHIPGLOBALS.GLOBAL.MBIN with a retuned flight model and installs
it as a loose-file mod under GAMEDATA/MODS/.

    ./build_mod.py            # build + install
    ./build_mod.py --diff     # show what would change vs vanilla
    ./build_mod.py --build    # build only
    ./build_mod.py --uninstall
"""
import argparse, shutil, sys
import xml.etree.ElementTree as ET
from pathlib import Path
import common as C

WORK     = C.WORK
MOD_NAME = "BetterFlight"
TARGET   = "GCSPACESHIPGLOBALS.GLOBAL.MBIN"
VANILLA  = WORK / "vanilla" / TARGET
MXML     = WORK / "GCSPACESHIPGLOBALS.GLOBAL.MXML"
BUILT    = WORK / "build" / TARGET

# ---------------------------------------------------------------------------
# TUNING
# ---------------------------------------------------------------------------
# Values are absolute, or ("mul", factor) to scale the vanilla value.
#
# SpaceEngine / CombatEngine       -> space flight   (aggressive)
# PlanetEngine / AtmosCombatEngine -> atmosphere     (gentler; the ground
#   proximity assists still need something to work with)

SPACE = {
    # NOTE: RollAmount is deliberately NOT touched. It is the strength of MANUAL
    # roll input (Q/E); zeroing it disabled roll entirely (v0.1 bug). Yaw->roll
    # banking is the RudderToRoll* group in GLOBAL_TUNING below.
    # Let the ship actually stop. MinSpeed is the forever-drifting-forward fix.
    "MinSpeed":          0.0,
    "MinSpeedForce":     0.0,
    # The flight assist. Vanilla bleeds off any velocity not aligned with the
    # nose; cutting this is what produces real momentum and drift.
    "DirectionBrake":    ("mul", 0.10),
    "DirectionBrakeMin": ("mul", 0.10),
    # Stop auto-levelling roll (time constant; larger = effectively off).
    "RollAutoTime":      1000.0,
    # Keep some authority so the ship still feels responsive to input.
    "TurnStrength":      ("mul", 1.6),
    "RollForce":         ("mul", 1.5),
}

ATMOS = {
    # Same as space: no minimum speed, so you can stop, hover and reverse in
    # atmosphere. (1.0.x kept a quarter of vanilla here - 5 m/s with a force
    # pushing back up to it - which made the ship creep and never reverse.)
    "MinSpeed":          0.0,
    "MinSpeedForce":     0.0,
    "DirectionBrake":    ("mul", 0.35),
    "DirectionBrakeMin": ("mul", 0.35),
    "RollAutoTime":      1000.0,
    "TurnStrength":      ("mul", 1.4),
    "RollForce":         ("mul", 1.5),
}

ENGINE_TUNING = {
    "SpaceEngine":      SPACE,
    "CombatEngine":     SPACE,
    "PlanetEngine":     ATMOS,
    "AtmosCombatEngine": ATMOS,
}

# Top-level globals. Same value convention.
GLOBAL_TUNING = {
    # Rudder->roll coupling: THIS is "turning the mouse banks the ship".
    "RudderToRollMultiplierMin":      0.0,
    "RudderToRollMultiplierMax":      0.0,
    "RudderToRollMultiplierOpposite": 0.0,
    "RudderToRollMultiplierSpace":    0.0,
    "RudderToRollMultiplierLow":      0.0,
    # Cosmetic roll induced by sideways drift - distracting once you can drift.
    "LateralDriftRollAmount":         0.0,
    # Passive velocity decay. Momentum should not evaporate.
    "LinearDamping":                  0.0,
    # Vertical component of the flight assist.
    "DirectionBrakeVerticalMultiplier": ("mul", 0.2),
}
# ----------------------------------------------------------------------------

# ---------------------------------------------------------------------------


def extract_vanilla():
    """Pull the pristine MBIN out of the game's pak (once per game build)."""
    if VANILLA.exists():
        return
    VANILLA.parent.mkdir(parents=True, exist_ok=True)
    tmp = WORK / "_extract"
    shutil.rmtree(tmp, ignore_errors=True)
    C.extract(C.PCBANKS / "NMSARC.globals.pak", "*spaceshipglobals*", tmp)
    src = next(tmp.rglob("gcspaceshipglobals.global.mbin"), None)
    if not src:
        sys.exit("extract failed")
    shutil.copy(src, VANILLA)
    shutil.rmtree(tmp, ignore_errors=True)
    print(f"  extracted vanilla -> {VANILLA.relative_to(C.ROOT)}")


def verify_roundtrip():
    """Hard gate: refuse to build if the compiler mangles this game's data."""
    ok, why = C.roundtrip_clean(C.MBINC, VANILLA, WORK / "_rt")
    if not ok:
        sys.exit(f"\n  ABORT: MBINCompiler round-trip is not lossless ({why}).\n"
                 f"  The installed toolchain does not match this game build.\n"
                 f"  Run:  ./setup_tools.py\n")
    print(f"  round-trip verified: {why}")


def decompile():
    src = WORK / TARGET
    shutil.copy(VANILLA, src)
    MXML.unlink(missing_ok=True)
    r = C.run([C.MBINC, "convert", "--overwrite", src])
    if "WARN" in r.stdout or not MXML.exists():
        sys.exit(f"decompile failed:\n{r.stdout}")
    print(f"  decompiled -> {MXML.name}")


def resolve(spec, current):
    if isinstance(spec, tuple) and spec[0] == "mul":
        return float(current) * spec[1]
    return float(spec)


def apply_tuning(show_diff=False):
    tree = ET.parse(MXML)
    root = tree.getroot()
    changes = []
    missing = []

    for block in root:
        if block.get("value") != "GcPlayerSpaceshipControlData":
            continue
        bname = block.get("name")
        for engine in block:
            ename = engine.get("name")
            tuning = ENGINE_TUNING.get(ename)
            if not tuning:
                continue
            seen = set()
            for prop in engine:
                pname = prop.get("name")
                if pname in tuning:
                    seen.add(pname)
                    old = prop.get("value")
                    new = resolve(tuning[pname], old)
                    prop.set("value", f"{new:.6f}")
                    changes.append((f"{bname}.{ename}.{pname}", old, f"{new:.6f}"))
            for k in set(tuning) - seen:
                missing.append(f"{bname}.{ename}.{k}")

    seen_g = set()
    for prop in root:
        pname = prop.get("name")
        if pname in GLOBAL_TUNING and len(list(prop)) == 0:
            seen_g.add(pname)
            old = prop.get("value")
            new = resolve(GLOBAL_TUNING[pname], old)
            prop.set("value", f"{new:.6f}")
            changes.append((pname, old, f"{new:.6f}"))
    missing += list(set(GLOBAL_TUNING) - seen_g)

    if missing:
        print(f"\n  WARNING: {len(missing)} tuning keys not found in this game build:")
        for m in sorted(missing)[:12]:
            print(f"      {m}")
        print("  The struct may have changed. Check field names against the MXML.\n")

    if show_diff:
        print(f"\n{len(changes)} changes:\n")
        w = max(len(c[0]) for c in changes)
        for name, old, new in changes:
            print(f"  {name:<{w}}  {old:>14} -> {new}")
        return None

    out = WORK / "modded.MXML"
    tree.write(out, encoding="utf-8", xml_declaration=True)
    print(f"  applied {len(changes)} changes")
    return out


def compile_mbin(mxml):
    BUILT.parent.mkdir(parents=True, exist_ok=True)
    staged = BUILT.parent / "GCSPACESHIPGLOBALS.GLOBAL.MXML"
    shutil.copy(mxml, staged)
    BUILT.unlink(missing_ok=True)
    r = C.run([C.MBINC, "convert", "--overwrite", staged])
    if not BUILT.exists():
        sys.exit(f"compile failed:\n{r.stdout}\n{r.stderr}")
    staged.unlink()
    print(f"  compiled -> {BUILT.name} ({BUILT.stat().st_size:,} bytes)")


def install():
    dest = C.MODS / MOD_NAME
    dest.mkdir(parents=True, exist_ok=True)
    shutil.copy(BUILT, dest / TARGET)
    print(f"\n  installed -> {dest}/{TARGET}")


def uninstall():
    dest = C.MODS / MOD_NAME
    if dest.exists():
        shutil.rmtree(dest)
        print(f"  removed {dest}")
    else:
        print("  not installed")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", action="store_true", help="build only, don't install")
    ap.add_argument("--uninstall", action="store_true")
    ap.add_argument("--diff", action="store_true", help="show changes vs vanilla")
    a = ap.parse_args()

    if a.uninstall:
        uninstall(); return

    st = C.require_current_toolchain()
    bid, nms = C.game_build()
    print("Better Flight — building")
    print(f"  game build {bid} / NMS {nms}   toolchain {st['mbincompiler']}\n")

    extract_vanilla()
    verify_roundtrip()
    decompile()

    if a.diff:
        apply_tuning(show_diff=True); return

    mxml = apply_tuning()
    compile_mbin(mxml)
    if not a.build:
        install()
        print("\n  Launch NMS. To revert:  ./build_mod.py --uninstall")


if __name__ == "__main__":
    main()
