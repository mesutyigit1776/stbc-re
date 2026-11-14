"""Check that the import gate gives the result `engine` to a host exit at engine code that is not reconstructed, and the
result `crash` to any other exit. With --defect the check uses the old rule (both are `crash`) and must fail."""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools"))
import import_gate  # noqa: E402

CASES = [
    (1, [], "not reconstructed: exe code at 0x00809fe0", "engine", "engine code not reconstructed at 0x00809fe0"),
    (134, [], "x\\nnot reconstructed: exe code at 0x007c6370\\n", "engine", "engine code not reconstructed at 0x007c6370"),
    (139, [], "Segmentation fault", "crash", "exit code 139: Segmentation fault"),
    (1, [], "", "crash", "exit code 1: no output"),
    (1, [["GATE", "startup"]], "boom", "startup", "start-up failed"),
]


def main():
    legacy = "--defect" in sys.argv
    failures = 0
    for code, gate, err, result, detail in CASES:
        got = import_gate.classify_exit(code, gate, err.replace("\\\\n", "\\n"), engine_class=not legacy)
        if got != (result, detail):
            failures += 1
            print("  expected %s, got %s" % ((result, detail), got))
    print("%s gate classes" % ("FAIL" if failures else "PASS"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
