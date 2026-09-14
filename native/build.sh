#!/usr/bin/env bash
# Build BetterFlight as a winmm.dll proxy with llvm-mingw (no sudo, no MSVC).
#   ./build.sh            -> build/winmm.dll
#   ./build.sh selftest   -> also build/selftest.exe and run it against the
#                            installed NMS.exe under Wine (never executes the game)
set -euo pipefail
cd "$(dirname "$0")"
TC=../tools/llvm-mingw/bin
CC=$TC/x86_64-w64-mingw32-clang
MH=third_party/minhook
SRCS=("$MH/src/buffer.c" "$MH/src/hook.c" "$MH/src/trampoline.c" "$MH/src/hde/hde64.c")
FLAGS=(-O2 -Wall -Wextra -Wno-unused-parameter -std=gnu11 -D_WIN32_WINNT=0x0601 -I "$MH/include" -I src -static -s)
mkdir -p build
# No version resource on purpose: Wine >= 11.6 then prefers it without an override.
$CC "${FLAGS[@]}" -shared src/betterflight.c src/winmm.def "${SRCS[@]}" -o build/winmm.dll
echo "built $(stat -c%s build/winmm.dll) bytes -> native/build/winmm.dll"

if [[ "${1:-}" == "selftest" ]]; then
    $CC "${FLAGS[@]}" -municode -DBF_SELFTEST src/betterflight.c "${SRCS[@]}" -o build/selftest.exe
    GAME_EXE="$(python3 -c 'import sys; sys.path.insert(0,".."); import common as C; print(C.GAME/"Binaries"/"NMS.exe")')"
    export WINEPREFIX="$PWD/build/wineprefix" WINEDEBUG=-all
    wine build/selftest.exe "$(winepath -w "$GAME_EXE")"
fi
