#!/usr/bin/env bash
# Baseline inventory of game/stbc.exe. Usage: re/baseline.sh [--record]
# REA_BASELINE_EXPECTED selects another expected file.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="$root/re/out/baseline"
exe="$root/game/stbc.exe"
expected="${REA_BASELINE_EXPECTED:-$root/re/baseline.expected}"

record=0
case "${1:-}" in
    "") ;;
    --record) record=1 ;;
    *) echo "usage: $0 [--record]" >&2; exit 2 ;;
esac

cd "$root"
shasum -a 256 -c checksums/stbc.exe.sha256 || { echo "exe hash check failed" >&2; exit 1; }

# shellcheck source=../tools/rea-env.sh
source tools/rea-env.sh
unset REA_GHIDRA_SEED_FILE REA_GHIDRA_LANGUAGE_ID REA_GHIDRA_COMPILER_SPEC_ID REA_GHIDRA_DISABLED_ANALYZERS

start=$(date +%s)
mkdir -p "$out"
rm -f "$out"/*.json "$out"/*.tsv "$out"/*.err

run_rea() {
    local name="$1"; shift
    rea "$@" --provider ghidra --format json --full-output >"$out/$name.json" 2>"$out/$name.err"
}

# '(?s)^' matches every value, including empty ones and ones with line breaks.
run_rea overview analyze "$exe" &
pid_overview=$!
run_rea procedures search "$exe" --kind procedures --mode regex --pattern '(?s)^' &
pid_procedures=$!
run_rea strings search "$exe" --kind strings --mode regex --pattern '(?s)^' &
pid_strings=$!

failed=0
for job in overview:$pid_overview procedures:$pid_procedures strings:$pid_strings; do
    wait "${job#*:}" || { echo "rea ${job%%:*} failed; see $out/${job%%:*}.err" >&2; failed=1; }
done
[ "$failed" -eq 0 ] || exit 1

summary="$out/summary.txt"
python3 -I - "$out" >"$summary" <<'PY'
import json, sys

out = sys.argv[1]


def load(name):
    with open(f"{out}/{name}.json") as f:
        doc = json.load(f)
    if not doc.get("ok"):
        sys.exit(f"{name}: REA reported failure")
    return doc["data"]


def escape(text):
    parts = []
    for ch in text:
        if ch == "\\":
            parts.append("\\\\")
        elif ch.isprintable() and ch != "\t":
            parts.append(ch)
        elif ch == "\t":
            parts.append("\\t")
        elif ch == "\n":
            parts.append("\\n")
        elif ch == "\r":
            parts.append("\\r")
        else:
            parts.append(f"\\u{ord(ch):04x}")
    return "".join(parts)


def write(name, header, rows):
    with open(f"{out}/{name}.tsv", "w", encoding="utf-8", newline="\n") as f:
        f.write("\t".join(header) + "\n")
        for row in rows:
            f.write("\t".join(escape(str(c)) for c in row) + "\n")
    return len(rows)


overview = load("overview")["normalized_result"]
procedures = load("procedures")["normalized_result"]
strings = load("strings")["normalized_result"]

segments = sorted(overview["segments"], key=lambda s: (int(s["start"], 16), s["name"]))
counts = {"segments": write(
    "segments", ["name", "start", "end", "length"],
    [(s["name"], s["start"], s["end"], s["length"]) for s in segments])}

internal = [p for p in procedures if not p["address"].startswith("EXTERNAL:")]
external = [p for p in procedures if p["address"].startswith("EXTERNAL:")]
internal.sort(key=lambda p: (int(p["address"], 16), p["value"]))
counts["procedures"] = write(
    "procedures", ["address", "name"], [(p["address"], p["value"]) for p in internal])
counts["names"] = write(
    "names", ["address", "name"],
    [(p["address"], p["value"]) for p in internal if not p["value"].startswith("FUN_")])

imports = []
for p in external:
    dll, _, func = p["value"].partition("::")
    imports.append((dll, func, p["address"]))
imports.sort(key=lambda i: (i[0], i[1], int(i[2].split(":")[1], 16)))
counts["imports"] = write("imports", ["dll", "function", "ref"], imports)

strings.sort(key=lambda s: (int(s["address"], 16), s["value"]))
counts["strings"] = write(
    "strings", ["address", "value"], [(s["address"], s["value"]) for s in strings])

if overview["procedure_count"] != len(procedures):
    sys.exit("overview procedure_count differs from the procedure search")
if overview["string_count"] != len(strings):
    sys.exit("overview string_count differs from the string search")
if overview["segment_count"] != len(segments):
    sys.exit("overview segment_count differs from the segment list")

print(f"image_base={overview['image_base']}")
for key in ("segments", "procedures", "names", "imports", "strings"):
    print(f"{key}={counts[key]}")
PY

echo "baseline inventory"
sed 's/^/  /' "$summary"
echo "  seconds=$(( $(date +%s) - start ))"

current="$out/current.expected"
{
    cat "$summary"
    for name in segments procedures names imports strings; do
        echo "sha256.$name=$(shasum -a 256 "$out/$name.tsv" | cut -d' ' -f1)"
    done
} >"$current"

if [ "$record" -eq 1 ]; then
    cp "$current" "$expected"
    echo "recorded $expected"
elif diff -u "$expected" "$current"; then
    echo "baseline matches $expected"
else
    echo "baseline differs from $expected" >&2
    exit 1
fi
