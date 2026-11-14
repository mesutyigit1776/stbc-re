"""Run the emulator tests.

Usage: tools/.venv/bin/python -I tests/emu/run.py [test name or prefix ...]
Exit status is 0 only if every selected test is green.
"""
import glob
import importlib.util
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import emuharness  # noqa: E402
from emuharness import registry  # noqa: E402


def load_tests():
    for path in sorted(glob.glob(os.path.join(HERE, "tests", "*.py"))):
        name = "emu_tests_" + os.path.splitext(os.path.basename(path))[0]
        spec = importlib.util.spec_from_file_location(name, path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)


def select(patterns):
    names = list(registry.REGISTRY)
    if not patterns:
        return names
    chosen = [n for n in names if any(n == p or n.startswith(p) for p in patterns)]
    unknown = [p for p in patterns if not any(n == p or n.startswith(p) for n in names)]
    if unknown:
        print("no test matches: " + ", ".join(unknown))
        sys.exit(2)
    return chosen


def main(argv):
    started = time.time()
    try:
        digest = emuharness.check_exe_hash()
    except Exception as exc:
        print("exe check failed: %s" % exc)
        return 2
    print("stbc.exe sha256 %s" % digest)
    load_tests()
    outcomes = []
    for name in select(argv):
        outcome = registry.run_entry(registry.REGISTRY[name])
        outcomes.append(outcome)
        print("%-6s %s  (%.2fs)" % (outcome.status, outcome.name, outcome.seconds))
        for line in outcome.lines:
            print(line)
    red = [o for o in outcomes if o.red]
    print("%d tests, %d red, %.1fs" % (len(outcomes), len(red), time.time() - started))
    return 1 if red else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
