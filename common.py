"""Shared plumbing: game discovery, build detection, toolchain resolution."""
import json, os, re, shutil, subprocess, sys, urllib.request
from pathlib import Path

ROOT   = Path(__file__).resolve().parent
WORK   = ROOT / "work"
TOOLS  = ROOT / "tools"
MBIN_DIR = TOOLS / "mbin"
MBINC  = MBIN_DIR / "MBINCompiler-linux"
HGPAK  = TOOLS / "hgpaktool"
DOTNET = Path.home() / ".dotnet"
STATE  = TOOLS / "toolchain.json"

APPID  = "275850"
GH     = "https://api.github.com/repos/monkeyman192/MBINCompiler"
DL     = "https://github.com/monkeyman192/MBINCompiler/releases/download"


def env():
    return dict(os.environ, DOTNET_ROOT=str(DOTNET),
                DOTNET_SYSTEM_GLOBALIZATION_INVARIANT="1",
                PATH=f"{DOTNET}:{os.environ['PATH']}")


def run(cmd, **kw):
    return subprocess.run([str(c) for c in cmd], env=env(),
                          capture_output=True, text=True, **kw)


# --------------------------------------------------------------- game discovery
def steam_libraries():
    libs = []
    override = os.environ.get("NMS_STEAM_LIB")
    if override:
        libs.append(Path(override))
    for base in (Path.home()/".steam/steam", Path.home()/".local/share/Steam"):
        vdf = base/"steamapps"/"libraryfolders.vdf"
        if vdf.exists():
            libs.append(base)
            libs += [Path(m) for m in re.findall(r'"path"\s+"([^"]+)"', vdf.read_text())]
    return libs


def find_game():
    override = os.environ.get("NMS_PATH")
    if override:
        return Path(override)
    for lib in steam_libraries():
        p = lib/"steamapps"/"common"/"No Man's Sky"
        if (p/"Binaries"/"NMS.exe").exists():
            return p
    sys.exit("Could not locate No Man's Sky. Set NMS_PATH=/path/to/No Man's Sky")


GAME    = find_game()
PCBANKS = GAME/"GAMEDATA"/"PCBANKS"
MODS    = GAME/"GAMEDATA"/"MODS"


def game_build():
    """Steam buildid, plus the in-game build number from the telemetry log."""
    build_id = None
    for lib in steam_libraries():
        acf = lib/"steamapps"/f"appmanifest_{APPID}.acf"
        if acf.exists():
            m = re.search(r'"buildid"\s+"(\d+)"', acf.read_text())
            if m:
                build_id = m.group(1)
            break
    nms_build = None
    log = GAME/"GAMEDATA"/"FullLog.txt"
    if log.exists():
        with open(log, "rb") as f:
            m = re.search(rb'"build":"(\d+)"', f.read(400_000))
            if m:
                nms_build = m.group(1).decode()
    return build_id, nms_build


def update_in_progress():
    for lib in steam_libraries():
        acf = lib/"steamapps"/f"appmanifest_{APPID}.acf"
        if acf.exists():
            flags = re.search(r'"StateFlags"\s+"(\d+)"', acf.read_text())
            if flags and int(flags.group(1)) != 4:      # 4 == fully installed
                return int(flags.group(1))
    return 0


# --------------------------------------------------------- toolchain resolution
def _get(url, tries=5):
    """Fetch bytes with retries - a concurrent Steam download makes this flaky."""
    import time
    last = None
    for n in range(tries):
        try:
            req = urllib.request.Request(url, headers={"User-Agent": "nms-flight-mod"})
            with urllib.request.urlopen(req, timeout=60) as r:
                return r.read()
        except Exception as e:
            last = e
            time.sleep(1.5 * (n + 1))
    raise RuntimeError(f"failed after {tries} tries: {url}\n  {last}")


def releases():
    return [d["tag_name"] for d in json.loads(_get(f"{GH}/releases?per_page=40"))]


def fetch_version(tag, dest):
    dest.mkdir(parents=True, exist_ok=True)
    for name in ("MBINCompiler-linux", "libMBIN-linux.so"):
        out = dest/name
        try:
            data = _get(f"{DL}/{tag}/{name}", tries=3)
        except Exception:
            return False
        if len(data) < 100_000:          # 404 page or truncated
            return False
        out.write_bytes(data)
    (dest/"MBINCompiler-linux").chmod(0o755)
    return True


def roundtrip_clean(compiler, mbin_src, scratch):
    """A version is correct iff decompile+recompile reproduces the data bytes."""
    scratch.mkdir(parents=True, exist_ok=True)
    for f in scratch.iterdir():
        f.unlink()
    work = scratch/mbin_src.name
    shutil.copy(mbin_src, work)
    r = run([compiler, "convert", "--overwrite", work])
    if "WARN" in r.stdout:
        return False, "template GUID mismatch"
    mxml = work.with_suffix(".MXML")
    if not mxml.exists():
        return False, "no MXML produced"
    if re.search(r'value="[-\d.]+E-(3[0-9]|4[0-9])"', mxml.read_text()):
        return False, "denormal floats (misaligned)"
    work.unlink()
    r = run([compiler, "convert", "--overwrite", mxml])
    rebuilt = mxml.with_suffix(".MBIN")
    if not rebuilt.exists():
        return False, "recompile failed"
    a, b = mbin_src.read_bytes(), rebuilt.read_bytes()
    if len(a) != len(b):
        return False, f"size differs ({len(a)} vs {len(b)})"
    diffs = [i for i in range(len(a)) if a[i] != b[i]]
    if any(i >= 0x20 for i in diffs):
        return False, f"{sum(1 for i in diffs if i>=0x20)} data bytes differ"
    return True, f"lossless ({len(a)-0x20:,} data bytes, {len(diffs)} header stamp bytes)"


def extract(pak, pattern, outdir):
    outdir.mkdir(parents=True, exist_ok=True)
    r = run([HGPAK, "--platform", "windows", "-f", pattern, "-O", outdir, pak])
    return r


def load_state():
    if STATE.exists():
        return json.loads(STATE.read_text())
    return {}


def save_state(d):
    STATE.parent.mkdir(parents=True, exist_ok=True)
    STATE.write_text(json.dumps(d, indent=2))


def require_current_toolchain():
    """Fail closed if the game changed since the toolchain was validated."""
    st = load_state()
    bid, nms = game_build()
    if st.get("steam_buildid") != bid or not MBINC.exists():
        sys.exit(
            f"\n  Toolchain not validated for the installed game.\n"
            f"    validated for : steam buildid {st.get('steam_buildid')} / NMS {st.get('nms_build')}\n"
            f"    installed now : steam buildid {bid} / NMS {nms}\n\n"
            f"  Run:  ./setup_tools.py\n")
    return st
