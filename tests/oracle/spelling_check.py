"""Checks that the oracle ignores only the number of exponent digits of float text and the 64-bit type of an integer
constant that the 32-bit type holds with the same low word.

Usage: python -I tests/oracle/spelling_check.py [--defect]
With --defect the exponent rule drops the text and the integer rule drops the value; the checks must then fail.
"""
import importlib.util
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
spec = importlib.util.spec_from_file_location("oracle", os.path.join(ROOT, "tools", "bytecode_oracle.py"))
oracle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(oracle)

if "--defect" in sys.argv:
    oracle.normalize_exponents = lambda text: text[:1]
    oracle.normalize_integers = lambda data: data[:1]


def body(*texts):
    return b"(" + struct.pack("<i", len(texts)) + b"".join(b"f" + bytes([len(t)]) + t for t in texts)


def same(a, b):
    return oracle.normalize_floats(body(*a)) == oracle.normalize_floats(body(*b))


def ints(*items):
    return b"(" + struct.pack("<i", len(items)) + b"".join(items)


def wide(value):
    return b"I" + struct.pack("<Q", value)


def narrow(value):
    return b"i" + struct.pack("<i", value)


def same_int(a, b):
    return oracle.normalize_integers(ints(*a)) == ints(*b)


checks = [
    ("3 digit and 2 digit exponent", same([b"6.7e-005"], [b"6.7e-05"])),
    ("positive exponent", same([b"1e+020", b"4.4e-005"], [b"1e+20", b"4.4e-05"])),
    ("other mantissa", not same([b"6.7e-005"], [b"6.8e-05"])),
    ("other exponent", not same([b"6.7e-005"], [b"6.7e-06"])),
    ("three significant exponent digits", same([b"1e+100"], [b"1e+100"]) and not same([b"1e+100"], [b"1e+10"])),
    ("plain text", not same([b"0.5"], [b"0.25"])),
    ("64-bit constant 0xffffffff", same_int([wide(0xFFFFFFFF)], [narrow(-1)])),
    ("64-bit constant 0x80000000", same_int([wide(0x80000000)], [narrow(-2 ** 31)])),
    ("other low word", not same_int([wide(0xFFFFFFFF)], [narrow(-2)])),
    ("high word is not zero", not same_int([wide(0x1FFFFFFFF)], [narrow(-1)])),
    ("32-bit constant stays", same_int([narrow(5)], [narrow(5)]) and not same_int([narrow(5)], [narrow(6)])),
]
failed = [name for name, ok in checks if not ok]
for name, ok in checks:
    print("%s %s" % ("PASS" if ok else "FAIL", name))
print("%s oracle spelling" % ("FAIL" if failed else "PASS"))
sys.exit(1 if failed else 0)
