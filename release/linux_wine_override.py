#!/usr/bin/env python3
"""
Better Flight - optional Linux / Steam Deck helper.

Better Flight's strafe module is Binaries/winmm.dll. Proton only loads a game's
own winmm.dll when told to. Do ONE of these:

  A) Steam -> No Man's Sky -> Properties -> Launch Options:
         WINEDLLOVERRIDES="winmm=n,b" %command%

  B) Close the game and run this script once. It writes the same setting into
     the game's Proton prefix, so no launch option is needed:
         python3 linux_wine_override.py            add
         python3 linux_wine_override.py --status   check
         python3 linux_wine_override.py --remove   undo

It only touches the "NMS.exe" DLL-override entry of the No Man's Sky prefix,
and backs up user.reg before the first change. Use "n,b", never plain "n".
"""
import argparse, re, shutil, sys, time
from pathlib import Path

APPID   = "275850"
SECTION = r"[Software\\Wine\\AppDefaults\\NMS.exe\\DllOverrides]"
LINE    = '"winmm"="native,builtin"\n'


def steam_roots():
    home = Path.home()
    return [home / ".steam" / "steam", home / ".local" / "share" / "Steam",
            home / ".var" / "app" / "com.valvesoftware.Steam" / ".local" / "share" / "Steam"]


def libraries():
    libs = []
    for root in steam_roots():
        vdf = root / "steamapps" / "libraryfolders.vdf"
        if vdf.exists():
            libs.append(root)
            libs += [Path(m) for m in re.findall(r'"path"\s+"([^"]+)"', vdf.read_text(errors="ignore"))]
    seen, out = set(), []
    for l in libs:
        k = str(l.resolve()) if l.exists() else str(l)
        if k not in seen:
            seen.add(k); out.append(l)
    return out


def find_user_reg():
    for lib in libraries():
        reg = lib / "steamapps" / "compatdata" / APPID / "pfx" / "user.reg"
        if reg.exists():
            return reg
    return None


def game_running():
    for p in Path("/proc").iterdir():
        if p.name.isdigit():
            try:
                cmd = (p / "cmdline").read_bytes().replace(b"\0", b" ")
            except OSError:
                continue
            if re.search(rb"Binaries[/\\]NMS\.exe", cmd):
                return True
    return False


def has_override(text):
    i = text.find(SECTION)
    if i < 0:
        return False
    block = text[i:].split("\n\n", 1)[0]
    return re.search(r'^"winmm"="native,builtin"$', block, re.M) is not None


def add(reg):
    text = reg.read_text(encoding="utf-8", errors="surrogateescape")
    if has_override(text):
        print("  already set - nothing to do")
        return
    backup = reg.with_suffix(".reg.betterflight-backup")
    if not backup.exists():
        shutil.copy2(reg, backup)
        print(f"  backed up {reg.name} -> {backup.name}")
    unix = int(time.time())
    ft = (unix + 11644473600) * 10_000_000
    i = text.find(SECTION)
    if i >= 0:
        head, rest = text[:i], text[i:]
        at = rest.find("\n") + 1
        if rest[at:].startswith("#time="):
            at = rest.find("\n", at) + 1
        text = head + rest[:at] + LINE + rest[at:]
    else:
        if not text.endswith("\n"):
            text += "\n"
        text += f"\n{SECTION} {unix}\n#time={ft:x}\n{LINE}"
    reg.write_text(text, encoding="utf-8", errors="surrogateescape")
    print('  added: NMS.exe DllOverrides "winmm"="native,builtin"')


def remove(reg):
    text = reg.read_text(encoding="utf-8", errors="surrogateescape")
    i = text.find(SECTION)
    if i < 0:
        print("  not set - nothing to do")
        return
    k = text.find("\n\n", i)
    end = len(text) if k < 0 else k + 1
    block = text[i:end]
    if LINE not in block:
        print("  not set - nothing to do")
        return
    block = block.replace(LINE, "", 1)
    values = [l for l in block.splitlines()[1:] if l and not l.startswith("#")]
    if values:
        text = text[:i] + block + text[end:]
    else:
        start = i - 1 if i >= 2 and text[i - 1] == "\n" and text[i - 2] == "\n" else i
        text = text[:start] + text[end:]
    reg.write_text(text, encoding="utf-8", errors="surrogateescape")
    print("  removed")


def main():
    ap = argparse.ArgumentParser(description="Set the Proton DLL override Better Flight needs.")
    ap.add_argument("--remove", action="store_true")
    ap.add_argument("--status", action="store_true")
    ap.add_argument("--prefix", help="path to a user.reg, if auto-detection fails")
    a = ap.parse_args()

    reg = Path(a.prefix) if a.prefix else find_user_reg()
    if not reg or not reg.exists():
        sys.exit("  Could not find the No Man's Sky Proton prefix.\n"
                 "  Launch the game once through Steam first, or use the launch option instead:\n"
                 '    WINEDLLOVERRIDES="winmm=n,b" %command%')
    print(f"  prefix: {reg}")
    if a.status:
        print(f"  override: {'SET' if has_override(reg.read_text(errors='surrogateescape')) else 'not set'}")
        return
    if game_running():
        sys.exit("  No Man's Sky is running. Quit it first - Proton rewrites this file on exit.")
    remove(reg) if a.remove else add(reg)


if __name__ == "__main__":
    main()
