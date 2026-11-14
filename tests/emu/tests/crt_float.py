"""Tests of the float to text and text to float conversions of the C run time of stbc.exe.

The original side runs sprintf (0x8599b9), _fltin2 (0x864877), strtod (0x85c28d) and atof (0x85a8da) in the
emulator. The native side is crt::Sprintf, crt::FltIn2, crt::Strtod and crt::Atof from build/libbc_emutest_p53 or
build/libbc_emutest_p24. Text is compared as bytes and doubles as bit patterns. Both libraries run at their own
x87 control word. The run also writes the exe results to tests/emu/build/crt_float_reference.tsv.
"""
import ctypes
import os
import random
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))

from math_common import CONTROL_WORDS, PRECISIONS, ensure_built, lib_path  # noqa: E402
from emuharness import PTR, F64, I32, Machine, Original, Stub, VOID, register  # noqa: E402
from emuharness import convert  # noqa: E402
from emuharness.case import EXE_PATH  # noqa: E402

SPRINTF = 0x8599B9
FLTIN2 = 0x864877
STRTOD = 0x85C28D
ATOF = 0x85A8DA
CFLTCVT_INIT = 0x859971
ERRNO = 0x85CBD7
CRT_RANGE = (0x850000, 0x30000)
REFERENCE_PATH = os.path.join(os.path.dirname(HERE), "build", "crt_float_reference.tsv")
ERRNO_SLOT = 0x30003000
SCRATCH_BUFFER = 0x20800000

CORE_FORMATS = ["%.17g", "%.12g", "%g", "%e", "%f", "%.2f"]
EXTRA_FORMATS = ["%.0f", "%#.0f", "%#.3g", "%.3e", "%.0e", "%#.0e", "%E", "%G", "%.1g", "%.0g", "%+.3f", "% .3f",
                 "%010.3f", "%-12.3e|", "%12.4G", "%.20f", "%.25g", "%#g", "%#.10g", "%.1f", "%.5e", "%-+9.2f|",
                 "%.3E", "%.2G", "%08.2e", "x=%.12g;", "(%.12g%+.12gj)"]


def double_bits(x):
    return struct.unpack("<Q", struct.pack("<d", x))[0]


def bits_double(b):
    return struct.unpack("<d", struct.pack("<Q", b))[0]


def make_doubles():
    rnd = random.Random(20260517)
    values = [0.0, -0.0, 1.0, -1.0, 0.5, 1.5, 2.5, 0.125, 0.375, 0.1, 0.2, 0.3, 1 / 3, 2 / 3, 6.7e-5, 6.7e-05,
              123456789012345678.0, 9.999999999999999e22, 1e22, 1e23, 5e-324, 2.2250738585072009e-308,
              2.2250738585072014e-308, 1.7976931348623157e308, 100.0, 99.99999, 0.0001, 0.00001, 1e15, 1e16,
              1e17, 123456.789, 0.9999999999999999, 9.5, 99.5, 0.05, 0.005, 1e-5, 12345678901234567890.0,
              4503599627370496.5, 2.0 ** 53, 2.0 ** 53 + 2, 0.30000000000000004]
    values += [float("1e%d" % k) for k in range(-323, 309)]
    values += [2.0 ** k for k in range(-1074, 1024, 3)]
    values += [bits_double(0x7FF0000000000000), bits_double(0xFFF0000000000000),
               bits_double(0x7FF8000000000000), bits_double(0xFFF8000000000000), bits_double(0x7FF4000000000000),
               bits_double(0xFFF4000000000000), bits_double(0x7FF8000000000001), bits_double(0x7FFFFFFFFFFFFFFF),
               bits_double(0xFFF0000000000001)]
    values += [rnd.randrange(1, 2 ** 52) * 2.0 ** -1074 for _ in range(120)]
    values += [rnd.randrange(1, 2 ** 8) * 2.0 ** -rnd.randrange(1, 80) for _ in range(150)]
    values += [rnd.randrange(1, 10 ** 17) / 10.0 ** rnd.randrange(0, 17) for _ in range(300)]
    values += [rnd.randrange(1, 2 ** 53) * 2.0 ** rnd.randrange(-1074, 970) for _ in range(150)]
    values += [bits_double(rnd.getrandbits(63)) for _ in range(900)]
    values += [-bits_double(rnd.getrandbits(63)) for _ in range(100)]
    values += [rnd.uniform(-1e5, 1e5) for _ in range(500)]
    values += [round(rnd.uniform(0, 1000), rnd.randrange(0, 6)) for _ in range(300)]
    return [v for v in values]


def make_texts():
    rnd = random.Random(11)
    texts = ["0", "1", "-1", "+1", "6.7e-005", "6.7e-5", "0.000067", ".5", "5.", "-.5", "+.5e+3", "1e", "1e+", "1e-",
             "1d5", "1D+5", "1E5", "0x10", "inf", "nan", "-", "+", ".", "", " ", "abc", "1.5abc", "00012", "0e0", "-0",
             "-0.0", "1e400", "-1e400", "1e-400", "-1e-400", "1e5000", "1e-5000", "1e99999999999", "1e-99999999999",
             "  \t\n\r12.5", "\x0b12", "\x0c12", "12 34", "1_0", "1.2.3", "1e5.5", "1e+-5", "--1", "+-1", "1.e5", "0.e5",
             "00.00e00", "0000000000000000000001", "0.0000000000000000000001", "1" + "0" * 400, "0." + "0" * 400 + "1",
             "4.9406564584124654e-324", "2.4703282292062327e-324", "2.4703282292062328e-324", "2.2250738585072014e-308",
             "2.2250738585072009e-308", "1.7976931348623157e308", "1.7976931348623158e308", "1.7976931348623159e308",
             "9007199254740993", "9007199254740995", "9007199254740997", "9007199254740992.5", "9007199254740993.0000001",
             "123456789012345678901234", "1234567890123456789012345", "1234567890123456789012355",
             "123456789012345678901235", "99999999999999999999999", "999999999999999999999995", "9999999999999999999999999",
             "0.1", "0.2", "0.3", "1e22", "1e23", "8.5e-5", "1.5e300", "123e-2", "123e+2", "1e0001", "1e0000", "1e00",
             "1e5e5", "1ee5", "5e-324", "1e-323", "3e-324", "7e-324", "2.5e-324", "4.9e-324", "1e308", "1e309",
             "179769313486231570814527423731704356798070567525844996598917476803157260780028538760589558632766878171540458953514382464234321326889464182768467546703537516986049910576551282076245490090389328944075868508455133942304583236903222948165808559332123348274797826204144723168738177180919299881250404026184124858368",
             "1e+0300", "1e-0300", "0.000000000000000000000000000000000000000000000000000000001e+60"]
    for k in range(-330, 310, 7):
        texts.append("1e%d" % k)
        texts.append("%de%d" % (rnd.randrange(1, 10 ** 20), k))
    for x in make_doubles()[:400]:
        if x == x and abs(x) != float("inf"):
            texts.append("%.17g" % x)
            texts.append("%.25e" % x)
            texts.append("%.40f" % x if abs(x) < 1e30 else "%.17e" % x)
            texts.append(repr(x))
    from fractions import Fraction
    from decimal import Decimal, getcontext
    getcontext().prec = 800
    for _ in range(60):
        x = bits_double(rnd.getrandbits(63) % 0x7FE0000000000000)
        y = bits_double(double_bits(x) + 1)
        mid = (Fraction(x) + Fraction(y)) / 2
        texts.append(str(Decimal(mid.numerator) / Decimal(mid.denominator)))
        texts.append("%.17g" % x)
    for base in ["9007199254740993", "9007199254740995", "18014398509481986", "18014398509481990", "4503599627370497.5",
                 "4503599627370496.5", "9007199254740993.5", "9007199254740993.25", "9007199254740993.125",
                 "9007199254740991.5", "1.000000000000000111022302462515654042363166809082031250"]:
        texts.append(base)
        whole, _, fraction = base.partition(".")
        for fill in ("0000000", "00000000", "000000000"):
            for tail in ("1", "4", "5", "9", "05", "50", "15", "95", "951", "049"):
                texts.append(whole + "." + fraction + fill + tail)
                texts.append(whole + fill + tail if not fraction else whole + "." + fraction + fill + tail + "e0")
    for _ in range(2500):
        digits = "".join(rnd.choice("0123456789") for _ in range(rnd.randrange(1, 32)))
        if rnd.random() < 0.7:
            cut = rnd.randrange(0, len(digits) + 1)
            digits = digits[:cut] + "." + digits[cut:]
        if rnd.random() < 0.6:
            digits += rnd.choice(["e", "E"]) + rnd.choice(["", "+", "-"]) + str(rnd.randrange(0, 340))
        if rnd.random() < 0.15:
            digits = rnd.choice(["-", "+"]) + digits
        if rnd.random() < 0.05:
            digits = " " + digits + rnd.choice(["", " ", "x", "e", "e+", "."])
        texts.append(digits)
    return texts


class Originals:
    """The exe functions in one emulator. The C run time float table is set up once."""

    def __init__(self, cw):
        self.cw = cw
        self.machine = Machine(EXE_PATH, stubs={ERRNO: Stub(self._errno)})
        self.machine.call(Original(CFLTCVT_INIT, 0x38, "cdecl", [], VOID, allow=[(CFLTCVT_INIT, 0x38)]), [], cw=cw)
        self.sprintf_fn = Original(SPRINTF, 0x40, "cdecl", [PTR, PTR, F64], I32, allow=[CRT_RANGE])
        self.fltin_fn = Original(FLTIN2, 0x80, "cdecl", [PTR, PTR, I32], PTR, allow=[CRT_RANGE])
        self.strtod_fn = Original(STRTOD, 0xAA, "cdecl", [PTR, PTR], F64, allow=[CRT_RANGE])
        self.atof_fn = Original(ATOF, 0x57, "cdecl", [PTR], F64, allow=[CRT_RANGE])

    @staticmethod
    def _errno(ctx):
        ctx.returns(ERRNO_SLOT)

    def _text(self, data):
        address = self.machine.heap.alloc(len(data) + 16)
        self.machine.write(address, data + b"\0")
        return address

    def _run(self, fn, values):
        return self.machine.call(fn, values, cw=self.cw, fresh=False)

    @staticmethod
    def _double(out):
        mantissa, sign_exp = struct.unpack("<QH", out.st0_raw)
        if mantissa == 0 and sign_exp & 0x7FFF == 0:
            return (sign_exp >> 15) << 63
        return convert.float80_to_bits(out.st0_raw, 52, 11)

    def sprintf(self, fmt, value):
        self.machine.heap.reset()
        buffer = self.machine.heap.alloc(2048)
        self.machine.write(buffer, b"\xAA" * 64)
        text = self._text(fmt.encode())
        self._run(self.sprintf_fn, [buffer, text, value])
        return self.machine.read(buffer, 1500).split(b"\0")[0]

    def fltin2(self, data):
        self.machine.heap.reset()
        record = self.machine.heap.alloc(64)
        self.machine.write(record, bytes(64))
        text = self._text(data)
        self._run(self.fltin_fn, [record, text, len(data)])
        flag, nbytes = struct.unpack("<ii", self.machine.read(record, 8))
        value = struct.unpack("<Q", self.machine.read(record + 16, 8))[0]
        return flag, nbytes, value

    def strtod(self, data):
        self.machine.heap.reset()
        end = self.machine.heap.alloc(16)
        text = self._text(data)
        self.machine.write(ERRNO_SLOT, bytes(4))
        out = self._run(self.strtod_fn, [text, end])
        offset = struct.unpack("<I", self.machine.read(end, 4))[0] - text
        error = struct.unpack("<I", self.machine.read(ERRNO_SLOT, 4))[0]
        return self._double(out), offset, error

    def atof(self, data):
        self.machine.heap.reset()
        text = self._text(data)
        out = self._run(self.atof_fn, [text])
        return self._double(out)


class Natives:
    def __init__(self, lib, sprintf_symbol="bc_fx_sprintf", fltin_symbol="bc_fx_fltin2"):
        self.lib = ctypes.CDLL(lib)
        self.sprintf_c = getattr(self.lib, sprintf_symbol)
        self.sprintf_c.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_double]
        self.sprintf_c.restype = ctypes.c_int
        self.fltin_c = getattr(self.lib, fltin_symbol)
        self.fltin_c.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
        self.strtod_c = self.lib.bc_fx_strtod
        self.strtod_c.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_int32), ctypes.POINTER(ctypes.c_int32)]
        self.strtod_c.restype = ctypes.c_double
        self.atof_c = self.lib.bc_fx_atof
        self.atof_c.argtypes = [ctypes.c_char_p]
        self.atof_c.restype = ctypes.c_double

    def sprintf(self, fmt, value):
        buffer = ctypes.create_string_buffer(2048)
        self.sprintf_c(buffer, fmt.encode(), value)
        return buffer.value

    def fltin2(self, data):
        record = ctypes.create_string_buffer(24)
        self.fltin_c(record, data, len(data))
        flag, nbytes = struct.unpack("<ii", record.raw[:8])
        return flag, nbytes, struct.unpack("<Q", record.raw[16:24])[0]

    def strtod(self, data):
        end, error = ctypes.c_int32(), ctypes.c_int32()
        value = self.strtod_c(data, ctypes.byref(end), ctypes.byref(error))
        return double_bits(value), end.value, 0x22 if error.value else 0

    def atof(self, data):
        return double_bits(self.atof_c(data))


class Comparison:
    def __init__(self, limit=6):
        self.total = 0
        self.bad = 0
        self.shown = []
        self.limit = limit

    def check(self, label, original, native):
        self.total += 1
        if original != native:
            self.bad += 1
            if len(self.shown) < self.limit:
                self.shown.append("%s: exe %r, native %r" % (label, original, native))

    def finish(self, name):
        assert self.bad == 0, "%s: %d of %d differ. %s" % (name, self.bad, self.total, "; ".join(self.shown))


def compare_text_output(precision, sprintf_symbol="bc_fx_sprintf", write_reference=False):
    lib = ensure_built(precision)
    cw = CONTROL_WORDS[precision]
    originals, natives = Originals(cw), Natives(lib, sprintf_symbol)
    result = Comparison()
    reference = []
    doubles = make_doubles()
    for fmt in CORE_FORMATS:
        for value in doubles:
            exe = originals.sprintf(fmt, value)
            result.check("%s of %r" % (fmt, value), exe, natives.sprintf(fmt, value))
            if write_reference:
                reference.append(("sprintf", fmt, "%016x" % double_bits(value), exe.decode("latin-1")))
    for fmt in EXTRA_FORMATS:
        for value in doubles[::5]:
            exe = originals.sprintf(fmt, value)
            if fmt.startswith("(") and fmt.count("%") == 2:
                continue
            result.check("%s of %r" % (fmt, value), exe, natives.sprintf(fmt, value))
            if write_reference:
                reference.append(("sprintf", fmt, "%016x" % double_bits(value), exe.decode("latin-1")))
    if write_reference:
        os.makedirs(os.path.dirname(REFERENCE_PATH), exist_ok=True)
        with open(REFERENCE_PATH, "a") as f:
            for row in reference:
                f.write("\t".join(row) + "\n")
    return result


def compare_text_input(precision, fltin_symbol="bc_fx_fltin2", write_reference=False):
    lib = ensure_built(precision)
    cw = CONTROL_WORDS[precision]
    originals, natives = Originals(cw), Natives(lib, fltin_symbol=fltin_symbol)
    result = Comparison()
    reference = []
    for text in make_texts():
        data = text.encode("latin-1")
        exe = originals.fltin2(data)
        result.check("_fltin2 of %r" % text, exe, natives.fltin2(data))
        exe_strtod = originals.strtod(data)
        native_strtod = natives.strtod(data)
        result.check("strtod of %r" % text, exe_strtod, native_strtod)
        result.check("atof of %r" % text, originals.atof(data), natives.atof(data))
        if write_reference:
            reference.append(("fltin2", text.encode("unicode_escape").decode(), str(exe[0]), "%d %016x" % (exe[1], exe[2])))
    if write_reference:
        with open(REFERENCE_PATH, "a") as f:
            for row in reference:
                f.write("\t".join(row) + "\n")
    return result


def reset_reference():
    os.makedirs(os.path.dirname(REFERENCE_PATH), exist_ok=True)
    with open(REFERENCE_PATH, "w") as f:
        f.write("kind\tinput\targument\texe_result\n")


@register("crt_float.sprintf_matches_exe_p53")
def sprintf_p53():
    reset_reference()
    compare_text_output(53, write_reference=True).finish("sprintf at 53 bits")


@register("crt_float.sprintf_matches_exe_p24")
def sprintf_p24():
    compare_text_output(24).finish("sprintf at 24 bits")


@register("crt_float.text_to_float_matches_exe_p53")
def input_p53():
    compare_text_input(53, write_reference=True).finish("text to float at 53 bits")


@register("crt_float.text_to_float_matches_exe_p24")
def input_p24():
    compare_text_input(24).finish("text to float at 24 bits")


@register("crt_float.sprintf_two_digit_exponent_differs", expect_fail=True)
def sprintf_defect():
    compare_text_output(53, "bc_fx_sprintf_defect_exponent").finish("planted two digit exponent")


@register("crt_float.text_to_float_one_ulp_differs", expect_fail=True)
def input_defect():
    compare_text_input(53, "bc_fx_fltin2_defect_ulp").finish("planted one ulp change")
