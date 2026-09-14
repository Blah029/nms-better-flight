#!/usr/bin/env bash
# Build BetterFlight as a winmm.dll proxy with llvm-mingw (no sudo, no MSVC).
#   ./build.sh            -> build/winmm.dll
#   ./build.sh selftest   -> also build/selftest.exe and run it against the
#                            installed NMS.exe under Wine (never executes the game)
#
# The export surface (every winmm function) is generated from native/reference/
# by gen_winmm_exports.py. Builds are reproducible: the linker's embedded
# timestamps are zeroed, so the same source always gives the same file hash.
set -euo pipefail
cd "$(dirname "$0")"
TC=../tools/llvm-mingw/bin
CC=$TC/x86_64-w64-mingw32-clang
MH=third_party/minhook
SRCS=("$MH/src/buffer.c" "$MH/src/hook.c" "$MH/src/trampoline.c" "$MH/src/hde/hde64.c")
FLAGS=(-O2 -Wall -Wextra -Wno-unused-parameter -std=gnu11 -D_WIN32_WINNT=0x0601 -I "$MH/include" -I src -static -s)
mkdir -p build
python3 gen_winmm_exports.py
# No version resource on purpose: Wine >= 11.6 then prefers it without an override.
$CC "${FLAGS[@]}" -shared src/betterflight.c src/winmm.def "${SRCS[@]}" ${EXTRA_CFLAGS:-} -o "${OUT:-build/winmm.dll}"

python3 - "${OUT:-build/winmm.dll}" <<'PY'
import struct, sys
p = sys.argv[1]; d = bytearray(open(p, "rb").read())
pe = struct.unpack_from("<I", d, 0x3C)[0]
struct.pack_into("<I", d, pe + 8, 0)                               # COFF TimeDateStamp
opt = pe + 24; nsec = struct.unpack_from("<H", d, pe + 6)[0]
optsz = struct.unpack_from("<H", d, pe + 20)[0]
exp_rva = struct.unpack_from("<I", d, opt + 112)[0]
secs = []
for i in range(nsec):
    o = pe + 24 + optsz + i * 40
    vsz, va, rsz, ptr = struct.unpack_from("<IIII", d, o + 8)
    secs.append((va, max(vsz, rsz), ptr))
if exp_rva:
    for va, sz, ptr in secs:
        if va <= exp_rva < va + sz:
            struct.pack_into("<I", d, ptr + (exp_rva - va) + 4, 0)   # export dir TimeDateStamp
open(p, "wb").write(bytes(d))
PY
echo "built $(stat -c%s "${OUT:-build/winmm.dll}") bytes -> ${OUT:-build/winmm.dll}"

if [[ "${1:-}" == "selftest" ]]; then
    $CC "${FLAGS[@]}" -municode -DBF_SELFTEST src/betterflight.c "${SRCS[@]}" -o build/selftest.exe
    GAME_EXE="$(python3 -c 'import sys; sys.path.insert(0,".."); import common as C; print(C.GAME/"Binaries"/"NMS.exe")')"
    export WINEPREFIX="$PWD/build/wineprefix" WINEDEBUG=-all
    wine build/selftest.exe "$(winepath -w "$GAME_EXE")"
fi
