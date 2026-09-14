#!/usr/bin/env bash
# Run this after every No Man's Sky update (with the game closed).
#
# 1. Revalidates MBINCompiler against the new game build (fails closed).
# 2. Rebuilds and reinstalls everything via install.py.
#
# The native DLL finds its hooks by signature at runtime, so it usually survives
# updates unchanged. If it doesn't, Binaries/BetterFlight.log says which
# signature failed, and the game still runs normally with strafe disabled.
set -euo pipefail
cd "$(dirname "$0")"
./setup_tools.py "$@"
./install.py
