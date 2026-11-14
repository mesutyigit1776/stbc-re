#!/bin/zsh
# Run a Python 1.5.2 script with the Appc module from the game folder:   port/appc/run.sh script.py [args]
# Env: APPC_TRACE=1 logs every unimplemented engine call. APPC_OVERLAY=<dir> uses another scripts folder (mod overlays).
# APPC_ROOT=<dir> runs from another game root (mods read scripts/Custom/... relative to it); APPC_DATA_ROOTS=<a:b> adds data folders.
R="$(cd "$(dirname "$0")/../.." && pwd)"; O="${APPC_BUILD:-$R/port/appc/build}"
export PYTHONHOME="$O/pyhome:$O/pyhome" PYTHONPATH="${APPC_OVERLAY:-scripts}:$R/port/appc/shim"
export APPC_PORT="$R/port/appc"
setopt pipefail 2>/dev/null; cd "${APPC_ROOT:-$R/game}" && "$O/python152_appc" "$@" 2>&1 | grep -v "^Could not find platform\|^Consider setting"
