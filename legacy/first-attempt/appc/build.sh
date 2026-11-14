#!/bin/zsh
# Build the Appc module into a CPython 1.5.2 interpreter (built-in module). Needs port/python152/build (run port/python152/build.sh first).
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; A="$ROOT/port/appc"; P="$ROOT/port/python152/build"; S="$P/src"; O="${APPC_BUILD:-$A/build}"
python3 "$ROOT/re/gen_appc.py" >/dev/null
python3 "$ROOT/re/gen_props.py" >/dev/null; python3 "$ROOT/re/gen_prop_layout.py" >/dev/null
python3 "$ROOT/re/gen_classes.py" >/dev/null
rm -rf "$O"; mkdir -p "$O"
CC="$P/bin/cc"
XF=(); [ -n "$ASAN" ] && XF=(-fsanitize=address)
# SDL3 (Homebrew; set SDL3_PREFIX to use another install): the platform layer port/platform/ needs its headers and library
SDL3_PREFIX="${SDL3_PREFIX:-$(brew --prefix sdl3 2>/dev/null)}"
[ -d "$SDL3_PREFIX/include/SDL3" ] || { echo "SDL3 not found: brew install sdl3 (or set SDL3_PREFIX)"; exit 1; }
# miniaudio (Homebrew, one header; set MINIAUDIO_PREFIX to use another install): the audio back end port/platform/audio.c
MINIAUDIO_PREFIX="${MINIAUDIO_PREFIX:-$(brew --prefix miniaudio 2>/dev/null)}"
[ -f "$MINIAUDIO_PREFIX/include/miniaudio/miniaudio.h" ] || { echo "miniaudio not found: brew install miniaudio (or set MINIAUDIO_PREFIX)"; exit 1; }
INC=(-I"$S/Include" -I"$S" -I"$A" -I"$A/gen" -I"$ROOT/port/engine" -I"$ROOT/port/platform" -I"$SDL3_PREFIX/include" -I"$MINIAUDIO_PREFIX/include/miniaudio" -I"$S/Include")
cc1() { $CC -O1 -g -fno-omit-frame-pointer "${XF[@]}" -c "${INC[@]}" "$1" -o "$O/$(basename "${1%.c}").o"; }
# the hand-written engine first: its defined eng_* symbols (also those made by macros) tell the stub generators what to skip
for f in "$ROOT"/port/engine/*.c; do cc1 "$f"; done
for f in "$ROOT"/port/platform/*.c; do cc1 "$f"; done
nm -gU "$O"/*.o | awk '$3 ~ /^_eng_/ {print substr($3, 6)}' | sort -u > "$A/gen/engine_defined.txt"
python3 "$ROOT/re/gen_ship_deleg.py" >/dev/null
python3 "$ROOT/re/gen_ui_stubs.py" >/dev/null
for f in "$A"/appc_runtime.c "$A"/gen/appc_wrappers.c "$A"/gen/appc_stubs.c "$A"/gen/appc_consts.c "$A"/gen/appc_types.c "$A"/gen/appc_globals.c "$A"/gen/appc_extras_table.c "$A"/gen/appc_props.c "$A"/gen/appc_props2.c "$A"/gen/appc_ui_stubs.c "$A"/gen/appc_ship_deleg.c; do
  cc1 "$f"
done
# built-in module table: copy of Modules/config.c plus Appc
sed -e 's|^extern void initimp();|&\nextern void initAppc();|' -e 's|\t{"regex", initregex},|\t{"Appc", initAppc},\n&|' "$S/Modules/config.c" > "$O/config_appc.c"
$CC -O1 "${XF[@]}" -c "${INC[@]}" "$O/config_appc.c" -o "$O/config_appc.o"
$CC "${XF[@]}" "$S/Modules/python.o" "$O"/*.o "$P/libpython1.5.a" -L"$SDL3_PREFIX/lib" -Wl,-rpath,"$SDL3_PREFIX/lib" -lSDL3 -ldl -lm -o "$O/python152_appc"
echo "built $O/python152_appc"
mkdir -p "$O/pyhome/lib"; ln -sfn "$S/Lib" "$O/pyhome/lib/python1.5"
echo "run with: PYTHONHOME=$O/pyhome $O/python152_appc"
