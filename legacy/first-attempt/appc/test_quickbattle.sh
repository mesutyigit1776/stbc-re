#!/bin/zsh
# Headless Quick Battle scenarios. Each line runs host_quickbattle.py with its arguments and prints the RESULT line.
# Stock game, then Orion Delta on a merged scripts root, then the window.
# Exit code 0 only if every scenario passes. Usage: port/appc/test_quickbattle.sh   (after port/appc/build.sh)
R="$(cd "$(dirname "$0")/../.." && pwd)"
fail=0
log=$(mktemp "${TMPDIR:-/tmp}/quickbattle.XXXXXX")
run() {
    local rc
    "$R/port/appc/run.sh" "$R/port/appc/host_quickbattle.py" "$@" > "$log" 2>&1; rc=$?
    echo "$(grep '^RESULT' "$log") [$*] exit $rc"
    [ $rc -eq 0 ] && grep -q '^RESULT PASS' "$log" || fail=1
}
# stock game: the game folder is the root
run Galaxy 0.5
run Galaxy 0.0 weak
run Galaxy 1.0
run Galaxy 1.0 weak
run BirdOfPrey 0.5
run BirdOfPrey 0.5 weak
# Remastered 1.2: its Quick Battle builds ship lists from Foundation definitions (QB_FOUNDATION=1) and registers its handlers on the XO
# (ET_START_SIMULATION to the XO, as in the stock game; Orion Delta registers on the XO's menu).
python3 "$R/port/appc/make_mod_root.py" remastered "$R/game" "$R/mods/remastered_1.2" > /dev/null || fail=1
export APPC_ROOT="$R/port/appc/roots/root_remastered" APPC_DATA_ROOTS="$R/mods/remastered_1.2:$R/game" QB_FOUNDATION=1
run Galaxy 0.5
run BirdOfPrey 1.0
unset APPC_ROOT APPC_DATA_ROOTS QB_FOUNDATION
# Orion Delta: the merged scripts root is the game folder with the Orion Delta scripts over it. The Orion Delta readme says to
# leave the Remastered scripts folder out; the data comes from Orion Delta, then Remastered, then the game.
python3 "$R/port/appc/make_mod_root.py" orion_delta "$R/game" "$R/mods/orion_delta_rc4" > /dev/null || fail=1
export APPC_ROOT="$R/port/appc/roots/root_orion_delta"
export APPC_DATA_ROOTS="$R/mods/orion_delta_rc4:$R/mods/remastered_1.2:$R/game"
export QB_START_DEST=menu QB_PLAYER=Constitution
run Bloodsport 0.5
run Bloodsport 1.0
unset APPC_ROOT APPC_DATA_ROOTS QB_START_DEST QB_PLAYER
# the keys drive the player (steer W, fire F) through the key bindings before the battle (host_quickbattle QB_INPUT)
QB_INPUT=1 run Galaxy 0.5
unset QB_INPUT
# the same battle in an SDL3 window, for 5 s of real time (needs a display session; checks the frame rate).
# QB_AUTOSTART=1 starts it without the Space or Return key press
QB_AUTOSTART=1 QB_WINDOW=1 QB_WINDOW_SECONDS=5 run Galaxy 0.5
rm -f "$log"
exit $fail
