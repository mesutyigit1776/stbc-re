#!/bin/zsh
# Mouse and keyboard tests of the menus for the stock game, Remastered and Orion Delta. Needs a display session.
# Usage: port/appc/test_input.sh   (after port/appc/build.sh). Exit code 0 only if everything passes.
R="$(cd "$(dirname "$0")/../.." && pwd)"
fail=0
python3 "$R/port/appc/make_mod_root.py" orion_delta "$R/game" "$R/mods/orion_delta_rc4" > /dev/null || fail=1
python3 "$R/port/appc/make_mod_root.py" remastered "$R/game" "$R/mods/remastered_1.2" > /dev/null || fail=1
log=$(mktemp "${TMPDIR:-/tmp}/input.XXXXXX")
run() {
    local setup="$1" root="$2" data="$3"
    ( [ -n "$root" ] && export APPC_ROOT="$root" APPC_DATA_ROOTS="$data"
      for t in test_input test_input_qb; do MENU_SETUP="$setup" "$R/port/appc/run.sh" "$R/port/appc/$t.py" 2>&1 | grep "^PASS\|^FAIL\|^RESULT\|Traceback"; done ) | tee -a "$log"
}
run stock "" ""
run rem "$R/port/appc/roots/root_remastered" "$R/mods/remastered_1.2:$R/game"
run orion "$R/port/appc/roots/root_orion_delta" "$R/mods/orion_delta_rc4:$R/mods/remastered_1.2:$R/game"
grep -c "^RESULT PASS" "$log" | grep -qx 6 || fail=1
grep -q "^FAIL\|Traceback" "$log" && fail=1
rm -f "$log"
exit $fail
