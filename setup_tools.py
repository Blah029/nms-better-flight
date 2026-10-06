#!/usr/bin/env python3
"""
Validate the toolchain against the *installed* game build.

MBINCompiler versions are tied to game versions. Using the wrong one does not
fail loudly - it emits silently misaligned data. So we don't guess: we probe
released versions against a real game file and keep the first one whose
decompile+recompile round-trip reproduces the data bytes exactly.

Run this after every No Man's Sky update.

    ./setup_tools.py            # probe and install the correct version
    ./setup_tools.py --force    # re-probe even if state looks current
    ./setup_tools.py --check    # report only, change nothing
"""
import argparse, shutil, sys
from pathlib import Path
import common as C

PROBE_FILE = "gcspaceshipglobals.global.mbin"   # the file we actually care about


def ensure_hgpaktool():
    if C.HGPAK.exists():
        return
    print("  fetching HGPAKtool ...")
    import gzip, urllib.request
    url = ("https://github.com/monkeyman192/HGPAKtool/releases/download/"
           "1.1.3/hgpaktool-x86_64-unknown-linux.gz")
    C.TOOLS.mkdir(parents=True, exist_ok=True)
    gz = C.TOOLS/"hgpaktool.gz"
    urllib.request.urlretrieve(url, gz)
    with gzip.open(gz) as f, open(C.HGPAK, "wb") as o:
        shutil.copyfileobj(f, o)
    gz.unlink()
    C.HGPAK.chmod(0o755)


def ensure_dotnet():
    if (C.DOTNET/"dotnet").exists():
        return
    print("  installing .NET 8 runtime (user-local, no sudo) ...")
    import urllib.request
    sh = C.WORK/"dotnet-install.sh"
    C.WORK.mkdir(parents=True, exist_ok=True)
    urllib.request.urlretrieve("https://dot.net/v1/dotnet-install.sh", sh)
    sh.chmod(0o755)
    r = C.run([sh, "--runtime", "dotnet", "--channel", "8.0",
               "--install-dir", C.DOTNET, "--no-path"])
    if not (C.DOTNET/"dotnet").exists():
        sys.exit(f"dotnet install failed:\n{r.stdout}\n{r.stderr}")


def get_probe_mbin():
    out = C.WORK/"probe"
    shutil.rmtree(out, ignore_errors=True)
    C.extract(C.PCBANKS/"NMSARC.globals.pak", "*spaceshipglobals*", out)
    src = next(out.rglob(PROBE_FILE), None)
    if not src:
        sys.exit("could not extract the probe file from NMSARC.globals.pak")
    keep = C.WORK/"probe_vanilla.mbin"
    shutil.copy(src, keep)
    shutil.rmtree(out, ignore_errors=True)
    return keep


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()

    bid, nms = C.game_build()
    print(f"\n  game     : {C.GAME}")
    print(f"  build    : steam {bid} / NMS {nms}")

    flags = C.update_in_progress()
    if flags:
        print(f"\n  ⚠  Steam reports an update in progress (StateFlags={flags}).")
        print("     Game files are not final. Re-run this once the update completes.")

    st = C.load_state()
    if a.check:
        ok = st.get("steam_buildid") == bid and C.MBINC.exists()
        print(f"  toolchain: {st.get('mbincompiler','(none)')} "
              f"validated for {st.get('steam_buildid','?')}")
        print(f"  status   : {'CURRENT' if ok else 'STALE - run ./setup_tools.py'}")
        return

    if flags and not a.force:
        return

    if st.get("steam_buildid") == bid and C.MBINC.exists() and not a.force:
        ensure_dotnet()    # /root/.dotnet is non-persistent - reinstall if wiped
        print(f"  toolchain: {st['mbincompiler']} already validated for this build")
        return

    ensure_dotnet()
    ensure_hgpaktool()
    probe = get_probe_mbin()
    print(f"  probe    : {probe.name} ({probe.stat().st_size:,} bytes)")

    tags = C.releases()
    print(f"\n  probing {len(tags)} MBINCompiler releases (newest first)\n")

    scratch = C.WORK/"probe_scratch"
    cache   = C.TOOLS/"candidates"
    winner  = None
    for tag in tags:
        d = cache/tag
        if not (d/"MBINCompiler-linux").exists():
            if not C.fetch_version(tag, d):
                print(f"    {tag:22s} no linux asset")
                continue
        ok, why = C.roundtrip_clean(d/"MBINCompiler-linux", probe, scratch)
        print(f"    {tag:22s} {'OK   ' if ok else 'no   '} {why}")
        if ok:
            winner = (tag, d)
            break

    if not winner:
        sys.exit("\n  No MBINCompiler release round-trips cleanly against this build.\n"
                 "  The game is likely newer than any release; wait for an update to\n"
                 "  monkeyman192/MBINCompiler.\n")

    tag, d = winner
    shutil.rmtree(C.MBIN_DIR, ignore_errors=True)
    shutil.copytree(d, C.MBIN_DIR)
    (C.MBIN_DIR/"MBINCompiler-linux").chmod(0o755)
    shutil.rmtree(cache, ignore_errors=True)
    shutil.rmtree(scratch, ignore_errors=True)

    # Extracted vanilla files are per-build. Wipe them so the next build
    # re-extracts from the updated paks instead of reusing stale data.
    stale = [C.WORK/"vanilla", C.WORK/"strafe", C.WORK/"build",
             C.WORK/"probe_vanilla.mbin", C.WORK/"_rt", C.WORK/"_extract"]
    wiped = 0
    for pth in stale:
        if pth.is_dir():
            shutil.rmtree(pth, ignore_errors=True); wiped += 1
        elif pth.exists():
            pth.unlink(); wiped += 1
    for junk in C.WORK.glob("*.MXML"):
        junk.unlink(); wiped += 1
    for junk in C.WORK.glob("*.MBIN"):
        junk.unlink(); wiped += 1
    if wiped:
        print(f"  cleared {wiped} stale cache entries (they belonged to the old build)")

    C.save_state({"steam_buildid": bid, "nms_build": nms, "mbincompiler": tag})
    print(f"\n  installed MBINCompiler {tag} -> tools/mbin/")
    print(f"  validated for steam buildid {bid} / NMS build {nms}")
    print("\n  Next:  ./build_mod.py && ./strafe_test.py\n")


if __name__ == "__main__":
    main()
