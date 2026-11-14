"""Import gate: import every script module in a fresh `bc_pyhost` process and record what blocks it.

For each source (stock game, Remastered, Orion Delta) the tool makes a game root in the work folder (the game
folder with the mod folder copied over it). It lists each `.pyc` or `.py` under `scripts/` as a dotted module name.
For each module it starts `bc_pyhost ROOT --gate MODULE`, which does the exe start-up up to the point the host has
(App, FixApp), the first lines of Autoexec up to the import of FontsAndIcons (E0243), and then imports the module.

Two passes:
  pass 1: a trap raises (the module stops at its first missing engine call);
  pass 2: a trap returns None (`--trap-none`), so the module shows all names it touches while it is imported.

Ignored: calls that run after the import (functions that the game loop calls later) and everything that needs
the engine at run time. A module that catches the error of a trap with try/except counts as ok in pass 1.

Run: tools/.venv/bin/python -I tools/import_gate.py --host build/bc_pyhost --work build/gate_work --out build/gate_out
"""
import argparse
import collections
import concurrent.futures
import os
import re
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCES = {
    "stock": os.path.join(ROOT, "game"),
    "remastered": os.path.join(ROOT, "mods", "remastered_1.2"),
    "orion": os.path.join(ROOT, "mods", "orion_delta_rc4"),
}
MAGIC_152 = b"\x99\x4e\x0d\x0a"
IDENTIFIER = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
TRAP_METHOD = re.compile(r"^Appc\.(\w+) is not reconstructed \(exe wrapper (0x[0-9a-f]+)\)$")
TRAP_ATTRIBUTE = re.compile(r"^Appc\.globals\.(\w+) is not reconstructed \(exe (getter|setter) (0x[0-9a-f]+)\)$")
TRAP_HELPER = re.compile(r"^App\.(\w+)\.(\w+) is not reconstructed \(exe function (0x[0-9a-f]+)\)$")
RESULT_CLASSES = ("ok", "trap", "error", "timeout", "engine", "crash")
ENGINE_ABORT = re.compile(r"not reconstructed: exe code at (0x[0-9a-f]+)")


def make_root(source, work, fresh):
    """The game root of one source in the work folder. Returns its path."""
    root = os.path.join(work, source)
    if os.path.exists(os.path.join(root, "scripts")) and not fresh:
        return root
    mod = SOURCES[source]
    game = SOURCES["stock"]
    subprocess.run([sys.executable, "-I", os.path.join(ROOT, "tools", "make_layered_root.py"), game,
                    mod if source != "stock" else game, root], check=True, stdout=subprocess.DEVNULL)
    return root


def precompile(host, root):
    """Write a .pyc for each .py without a valid one, as the import code of the exe does at the first import.

    This keeps the gate processes from writing the same file at the same time."""
    jobs = []
    for folder, _, names in os.walk(os.path.join(root, "scripts")):
        for name in names:
            if not name.endswith(".py"):
                continue
            path = os.path.join(folder, name)
            mtime = int(os.stat(path).st_mtime)
            try:
                head = open(path + "c", "rb").read(8)
            except OSError:
                head = b""
            if len(head) == 8 and head[:4] == MAGIC_152 and struct.unpack("<I", head[4:])[0] == mtime:
                continue
            jobs.append((path, mtime))
    for start in range(0, len(jobs), 100):
        chunk = jobs[start:start + 100]
        text = "".join("%s\t%s\n" % (p, os.path.basename(p)) for p, _ in chunk)
        data = subprocess.run([host, "--compile-oracle"], input=text.encode("latin-1"), stdout=subprocess.PIPE,
                              stderr=subprocess.DEVNULL).stdout
        pos = 0
        for path, mtime in chunk:
            end = data.index(b"\n", pos)
            tag, size = data[pos:end].decode().split()
            pos = end + 1
            body = data[pos:pos + int(size)]
            pos += int(size)
            if tag == "OK":
                with open(path + "c", "wb") as handle:
                    handle.write(MAGIC_152 + struct.pack("<I", mtime) + body)
    return len(jobs)


def list_modules(root):
    """Dotted module names of every .py or .pyc under scripts/. Returns (modules, skipped)."""
    base = os.path.join(root, "scripts")
    modules, skipped = {}, []
    for folder, _, names in os.walk(base):
        for name in sorted(names):
            stem, ext = os.path.splitext(name)
            if ext not in (".py", ".pyc"):
                continue
            parts = os.path.relpath(folder, base).split(os.sep)
            parts = [] if parts == ["."] else parts
            if stem != "__init__":
                parts = parts + [stem]
            if not parts:
                continue
            dotted = ".".join(parts)
            if not all(IDENTIFIER.match(p) for p in parts):
                skipped.append(os.path.relpath(os.path.join(folder, name), base))
                continue
            modules[dotted] = True
    return sorted(modules), skipped


def parse_trap(text):
    """(name, kind, address) of a trap text, or None."""
    m = TRAP_METHOD.match(text)
    if m:
        return m.group(1), "method", m.group(2)
    m = TRAP_ATTRIBUTE.match(text)
    if m:
        return "globals." + m.group(1), "attribute " + m.group(2), m.group(3)
    m = TRAP_HELPER.match(text)
    if m:
        return "App.%s.%s" % (m.group(1), m.group(2)), "helper", m.group(3)
    return None


def classify_exit(returncode, gate, err, engine_class=True):
    """(result, detail) of a host run that ended without a normal gate line. A host exit at engine code that the source
    does not have is the result `engine`; any other exit is `crash`; a failed start-up is `startup`."""
    abort = ENGINE_ABORT.search(err)
    if abort:
        return ("engine" if engine_class else "crash"), "engine code not reconstructed at %s" % abort.group(1)
    if gate and gate[0][1] == "startup":
        return "startup", "start-up failed"
    tail = [l for l in err.splitlines() if l.strip()][-1:] or ["no output"]
    return "crash", "exit code %d: %s" % (returncode, tail[0][:150])


def run_module(host, root, module, none_mode, timeout):
    """Run one module. Returns (result, trap text, detail, events)."""
    args = [host, root, "--gate", module] + (["--trap-none"] if none_mode else [])
    try:
        proc = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout, cwd=root)
    except subprocess.TimeoutExpired:
        return "timeout", "", "no result after %d s" % timeout, []
    out = proc.stdout.decode("latin-1").splitlines()
    gate = [line.split("\t") for line in out if line.startswith("GATE\t")]
    events = [line.split("\t", 1)[1] for line in out if line.startswith("EVENT\t")]
    if proc.returncode != 0 or not gate:
        result, detail = classify_exit(proc.returncode, gate, proc.stderr.decode("latin-1"))
        return result, "", detail, events
    fields = gate[0] + [""] * 5
    result, trap, kind, text = fields[1], fields[2], fields[3], fields[4]
    detail = "" if result == "ok" else ("%s: %s" % (kind, text) if kind else text)
    return result, trap, detail, events


def run_pass(host, roots, modules, none_mode, timeout, jobs):
    tasks = [(source, m) for source in roots for m in modules[source]]
    results = {}

    def work(task):
        source, module = task
        return task, run_module(host, roots[source], module, none_mode, timeout)

    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        for task, outcome in pool.map(work, tasks):
            results[task] = outcome
    return results


def write_tsv(path, header, rows):
    with open(path, "w") as handle:
        handle.write("\t".join(header) + "\n")
        for row in rows:
            handle.write("\t".join(str(c) for c in row) + "\n")


def interface_class(name, classes):
    """The interface class of an Appc name, from its prefix."""
    if name.startswith("globals."):
        return "globals"
    if name.startswith("App."):
        return name.split(".")[1]
    stem = name
    for prefix in ("new_", "delete_"):
        if stem.startswith(prefix):
            return stem[len(prefix):]
    best = ""
    for c in classes:
        if stem.startswith(c + "_") and len(c) > len(best):
            best = c
    if best:
        return best
    return stem.split("_")[0] if "_" in stem else "(module function)"


def read_classes():
    names = set()
    with open(os.path.join(ROOT, "target", "appc_swig_types.tsv")) as handle:
        next(handle)
        for line in handle:
            f = line.rstrip("\n").split("\t")
            if len(f) > 4 and f[4].startswith("_p_"):
                names.add(f[4][3:])
    return names


def read_use():
    """Static use of each wrapper: sites from wrapper_use.tsv, distinct files from wrapper_files.tsv."""
    sites, files = {}, {}
    with open(os.path.join(ROOT, "target", "wrapper_use.tsv")) as handle:
        next(handle)
        for line in handle:
            f = line.rstrip("\n").split("\t")
            sites[f[0]] = int(f[6]) + int(f[7]) + int(f[9]) + int(f[10])
    path = os.path.join(ROOT, "target", "wrapper_files.tsv")
    if os.path.exists(path):
        with open(path) as handle:
            next(handle)
            for line in handle:
                f = line.rstrip("\n").split("\t")
                files[f[0]] = int(f[3]) + int(f[4])
    return sites, files


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--work", required=True, help="folder for the game roots (made again with --fresh)")
    parser.add_argument("--out", required=True, help="folder for the result files")
    parser.add_argument("--source", action="append", choices=sorted(SOURCES))
    parser.add_argument("--module", action="append", help="import only these modules (a sample)")
    parser.add_argument("--timeout", type=int, default=30)
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    parser.add_argument("--fresh", action="store_true")
    parser.add_argument("--no-second-pass", action="store_true")
    parser.add_argument("--expect", help="a TSV of known results (source, module, result, first_missing_name): "
                        "compare pass 1 with it, write nothing else, exit 1 on a difference")
    parser.add_argument("--defect", action="store_true",
                        help="with --expect: run pass 1 with traps that return None, so that known results change")
    args = parser.parse_args()
    host = os.path.abspath(args.host)
    os.makedirs(args.work, exist_ok=True)
    os.makedirs(args.out, exist_ok=True)
    sources = args.source or ["stock", "remastered", "orion"]

    roots, modules = {}, {}
    for source in sources:
        roots[source] = os.path.abspath(make_root(source, args.work, args.fresh))
        made = precompile(host, roots[source])
        found, skipped = list_modules(roots[source])
        modules[source] = [m for m in found if args.module is None or m in args.module]
        print("%s: %d modules, %d names skipped, %d .pyc written for sources without one"
              % (source, len(modules[source]), len(skipped), made), flush=True)

    first = run_pass(host, roots, modules, args.defect, args.timeout, args.jobs)
    if args.expect:
        sys.exit(check_expected(args.expect, first))
    second = None if args.no_second_pass else run_pass(host, roots, modules, True, args.timeout, args.jobs)

    rows = []
    for (source, module), (result, trap, detail, events) in sorted(first.items()):
        info = parse_trap(trap) if trap else None
        rows.append((source, module, result, info[0] if info else "", detail))
    write_tsv(os.path.join(args.out, "import_gate.tsv"), ["source", "module", "result", "first_missing_name", "detail"], rows)
    if second is not None:
        rows2 = []
        for (source, module), (result, trap, detail, events) in sorted(second.items()):
            seen = []
            for e in events:
                info = parse_trap(e)
                if info and info[0] not in seen:
                    seen.append(info[0])
            rows2.append((source, module, result, seen[0] if seen else "", len(seen), ",".join(seen), detail))
        write_tsv(os.path.join(args.out, "import_gate_touched.tsv"),
                  ["source", "module", "result", "first_missing_name", "touched_count", "touched_names", "detail"],
                  rows2)

    print_totals(first, second, sources)
    write_missing(args.out, first, second, sources)


def check_expected(path, first):
    """Compare pass 1 with a file of known results. Returns the exit code."""
    failures = 0
    with open(path) as handle:
        next(handle)
        for line in handle:
            source, module, result, name = (line.rstrip("\n").split("\t") + [""])[:4]
            got = first.get((source, module))
            info = parse_trap(got[1]) if got and got[1] else None
            actual = (got[0], info[0] if info else "") if got else ("missing", "")
            if actual != (result, name):
                failures += 1
                print("  %s %s: expected %s %s, got %s %s" % (source, module, result, name, actual[0], actual[1]))
    print("%s gate sample: known results" % ("FAIL" if failures else "PASS"))
    return 1 if failures else 0


def print_totals(first, second, sources):
    for label, data in (("pass 1 (traps raise)", first), ("pass 2 (traps return None)", second)):
        if data is None:
            continue
        print(label)
        for source in sources:
            counts = collections.Counter(v[0] for (s, _), v in data.items() if s == source)
            print("  %-10s ok %d, trap %d, error %d, timeout %d, engine %d, crash %d, other %d" % (
                source, counts["ok"], counts["trap"], counts["error"], counts["timeout"], counts["engine"],
                counts["crash"], sum(counts.values()) - sum(counts[k] for k in RESULT_CLASSES)))


def write_missing(out, first, second, sources):
    classes = read_classes()
    sites, files = read_use()
    blocked = collections.defaultdict(lambda: collections.Counter())
    kinds, addresses = {}, {}
    for (source, module), (result, trap, detail, events) in first.items():
        info = parse_trap(trap) if result == "trap" else None
        if info:
            blocked[info[0]][source] += 1
            kinds[info[0]], addresses[info[0]] = info[1], info[2]
    touched = collections.defaultdict(lambda: collections.Counter())
    if second is not None:
        for (source, module), (result, trap, detail, events) in second.items():
            seen = set()
            for e in events:
                info = parse_trap(e)
                if info:
                    seen.add(info[0])
                    kinds.setdefault(info[0], info[1])
                    addresses.setdefault(info[0], info[2])
            for name in seen:
                touched[name][source] += 1
    header = ["name", "kind", "exe_wrapper_address", "interface_class", "blocked_stock", "blocked_remastered",
              "blocked_orion", "blocked_total", "static_sites_remastered_orion", "static_files_remastered_orion",
              "touched_stock", "touched_remastered", "touched_orion"]
    rows = []
    for name in set(blocked) | set(touched):
        b = blocked[name]
        t = touched[name]
        rows.append((name, kinds[name], addresses[name], interface_class(name, classes),
                     b["stock"], b["remastered"], b["orion"], sum(b.values()),
                     sites.get(name, 0), files.get(name, 0),
                     t["stock"], t["remastered"], t["orion"]))
    rows.sort(key=lambda r: (-(r[5] + r[6]), -r[9], -r[8], r[0]))
    write_tsv(os.path.join(out, "missing_names.tsv"), header, [r for r in rows if r[7] > 0])
    rows.sort(key=lambda r: (-(r[11] + r[12]), -(r[5] + r[6]), -r[9], r[0]))
    write_tsv(os.path.join(out, "touched_names.tsv"), header, rows)
    print("names that block an import (pass 1): %d; names touched at import (pass 2): %d"
          % (sum(1 for r in rows if r[7] > 0), len(rows)))


if __name__ == "__main__":
    main()
