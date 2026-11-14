#!/bin/zsh
# Menu tests: every menu screen in four screen shapes (4:3, 16:10, 16:9, 21:9) for the stock game, Remastered and Orion
# Delta; the display settings of the Graphics tab; the Load Game file list; the credits. Snapshots go to $MENU_OUT (default a temp folder).
# Needs a display session. Usage: port/appc/test_menus.sh   (after port/appc/build.sh). Exit code 0 only if everything passes.
R="$(cd "$(dirname "$0")/../.." && pwd)"
export MENU_OUT="${MENU_OUT:-$(mktemp -d "${TMPDIR:-/tmp}/menus.XXXXXX")}"; mkdir -p "$MENU_OUT"
fail=0
python3 "$R/port/appc/make_mod_root.py" orion_delta "$R/game" "$R/mods/orion_delta_rc4" > /dev/null || fail=1
python3 "$R/port/appc/make_mod_root.py" remastered "$R/game" "$R/mods/remastered_1.2" > /dev/null || fail=1
run() {
    local setup="$1" root="$2" data="$3"
    ( [ -n "$root" ] && export APPC_ROOT="$root" APPC_DATA_ROOTS="$data"
      for t in test_menus test_menus_qb; do MENU_SETUP="$setup" "$R/port/appc/run.sh" "$R/port/appc/$t.py" 2>&1 | grep "^PASS\|^FAIL\|^RESULT\|Traceback\|^ui_display:"; done ) | tee -a "$MENU_OUT/log.txt"
}
run stock "" ""
run rem "$R/port/appc/roots/root_remastered" "$R/mods/remastered_1.2:$R/game"
run orion "$R/port/appc/roots/root_orion_delta" "$R/mods/orion_delta_rc4:$R/mods/remastered_1.2:$R/game"
grep -c "^RESULT PASS" "$MENU_OUT/log.txt" | grep -qx 6 || fail=1
grep -q "Traceback\|^ui_display:" "$MENU_OUT/log.txt" && fail=1
grep -c "^FAIL" "$MENU_OUT/log.txt" | grep -qx 0 || fail=1
python3 "$R/port/tools_ui_check.py" "$MENU_OUT" || fail=1
echo "snapshots: $MENU_OUT"
exit $fail
