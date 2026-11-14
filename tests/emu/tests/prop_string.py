"""Tests of TGString (0x6f3e90 and the functions around it), the helpers of its VC6 string template and the
wide string functions of the C run time.

A string has a shared, counted buffer, so many results depend on the state of the buffer: no buffer, a frozen
buffer (made from a narrow string), an unshared copy, a buffer shared by two or three strings. The tests bring
the strings into these states with the code of the exe (constructor, AssignString, AssignNarrow), on both sides.
Each setup call is compared too. Probes read the share count byte, the sharing pattern and the terminator.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from prop_common import *  # noqa: F401,F403,E402
from prop_common import (STRING_CODE, failing_text, narrow, narrow_text, orig, pool_stubs_zeroed, span,  # noqa: E402
                         string_state, wide)

def code(first, last, args, ret, conv="thiscall"):
    return orig(first, last, conv, args, ret, allow=STRING_CODE)


CTOR = code(0x6F3E90, 0x6F3EE1, [PTR, Buf(io="in")], PTR)
CTOR_NULL = code(0x6F3E90, 0x6F3EE1, [PTR, PTR], PTR)
DTOR = code(0x6F3EF0, 0x6F3F2E, [PTR], None)
ASSIGN_NARROW = code(0x6F47C0, 0x6F4966, [PTR, Buf(io="in")], None)
ASSIGN_NARROW_NULL = code(0x6F47C0, 0x6F4966, [PTR, PTR], None)
ASSIGN_CHECKED = code(0x6F4CC0, 0x6F4CD0, [PTR, Buf(io="in")], None)
ASSIGN_CHECKED_NULL = code(0x6F4CC0, 0x6F4CD0, [PTR, PTR], None)
ASSIGN_STRING = code(0x6F4EE0, 0x6F5019, [PTR, PTR], None)
COMPARE = code(0x6F4450, 0x6F44AD, [PTR, PTR, U8], I32)
COMPARE_C = code(0x6F43E0, 0x6F444B, [PTR, Buf(io="in"), I32], I32)
COMPARE_C_NULL = code(0x6F43E0, 0x6F444B, [PTR, PTR, I32], I32)
NEW_NARROW_COPY = code(0x6F56F0, 0x6F5737, [PTR], PTR)

TIDY = code(0x4166D0, 0x416718, [PTR, U8], None)
EOS = code(0x416720, 0x416732, [PTR, U32], None)
GROW = code(0x416740, 0x416861, [PTR, U32, U8], U8)
COPY_BUFFER = code(0x416870, 0x41697F, [PTR, U32], None)
SPLIT = code(0x416980, 0x416A55, [PTR], None)
ASSIGN_CHARS = code(0x6F5820, 0x6F58C1, [PTR, Buf(io="in"), U32], PTR)

SETUP_CALLS = {
    "ctor": SetupCall(CTOR, Native(LIB, "bc_TGString_TGString")),
    "assign": SetupCall(ASSIGN_STRING, Native(LIB, "bc_TGString_AssignString")),
    "narrow": SetupCall(ASSIGN_NARROW, Native(LIB, "bc_TGString_AssignNarrow")),
}

TEXTS = ["", "a", "abc", "abcd", "hello world", "caf\xe9", "\x80\xff\x01", "p" * 30, "q" * 31, "r" * 32, "s" * 33,
         "t" * 62, "u" * 63, "v" * 64, "w" * 100]
SHORT_TEXTS = ["", "a", "abc", "q" * 31, "r" * 32, "w" * 100]
STATES = ["none", "frozen", "unshared", "shared", "shared3", "emptied"]
HELPERS = ("t", "u", "v")


def strings(**given):
    roots = {name: Obj("TGString") for name in ("s",) + HELPERS}
    roots.update(given)
    return roots


def state(kind, text):
    """Setup calls that bring the string s into a state. t, u and v are helper strings."""
    ctor = lambda name, value: Pre("ctor", [Root(name), narrow(value)])
    assign = lambda dst, src: Pre("assign", [Root(dst), Root(src)])
    if kind == "none":
        return []
    if kind == "frozen":
        return [ctor("s", text)]
    if kind == "unshared":
        return [ctor("t", text), assign("s", "t")]
    if kind == "shared":
        return [ctor("t", text), assign("u", "t"), assign("s", "u")]
    if kind == "shared3":
        return [ctor("t", text), assign("u", "t"), assign("s", "u"), assign("v", "u")]
    if kind == "emptied":
        return [ctor("s", text), Pre("narrow", [Root("s"), narrow("")])]
    raise ValueError(kind)


def scenario(kind, text, args, label=None, roots=None):
    return Scenario(roots or strings(), args, pre=state(kind, text),
                    label=label or "%s %r" % (kind, text if len(text) < 12 else text[:8] + "..."))


PROBE = string_state("s", *HELPERS)


def case(original, symbol, scenarios, ret_class=None, probes=(PROBE,), label="", allow_leaks=False):
    return ObjectCase(MODEL, original, Native(LIB, symbol), scenarios, setup_calls=SETUP_CALLS, probes=list(probes),
                      stubs=pool_stubs_zeroed(), ret_class=ret_class, label=label, allow_leaks=allow_leaks)


# TGString::TGString ------------------------------------------------------------------------------------------

@register("prop.string.TGString_TGString")
def ctor_test():
    scenarios = [Scenario(strings(), [Root("s"), narrow(t)], label=repr(t[:12])) for t in TEXTS]
    return [case(CTOR, "bc_TGString_TGString", scenarios, ret_class="TGString"),
            case(CTOR_NULL, "bc_TGString_TGString", [Scenario(strings(), [Root("s"), 0], label="null")],
                 ret_class="TGString")]


@register("prop.string.TGString_destructor")
def dtor_test():
    scenarios = [scenario(kind, text, [Root("s")]) for kind in STATES for text in SHORT_TEXTS]
    return case(DTOR, "bc_TGString_Destroy", scenarios)


# AssignNarrow and AssignNarrowChecked ------------------------------------------------------------------------

@register("prop.string.TGString_AssignNarrow")
def assign_narrow_test():
    scenarios = [scenario(kind, start, [Root("s"), narrow(text)],
                          label="%s %r <- %r" % (kind, start[:8], text[:8]))
                 for kind in STATES for start in ("", "abc", "m" * 40) for text in TEXTS]
    return [case(ASSIGN_NARROW, "bc_TGString_AssignNarrow", scenarios),
            case(ASSIGN_NARROW_NULL, "bc_TGString_AssignNarrow",
                 [scenario(kind, "abc", [Root("s"), 0], label=kind + " <- null") for kind in STATES])]


@register("prop.string.TGString_AssignNarrowChecked")
def assign_checked_test():
    scenarios = [scenario(kind, "abc", [Root("s"), narrow(text)]) for kind in STATES for text in ("", "xy", "z" * 50)]
    return [case(ASSIGN_CHECKED, "bc_TGString_AssignNarrowChecked", scenarios),
            case(ASSIGN_CHECKED_NULL, "bc_TGString_AssignNarrowChecked",
                 [scenario(kind, "abc", [Root("s"), 0], label=kind + " <- null") for kind in STATES])]


# AssignString --------------------------------------------------------------------------------------------------

def two_string_scenarios():
    """s is the target, d... the source states are built on the helper strings with the same setup calls."""
    out = []
    for dst_kind in STATES:
        for dst_text in ("", "xy", "n" * 40):
            for src_kind, src_text in (("none", ""), ("frozen", "abc"), ("frozen", "z" * 45), ("unshared", "hello"),
                                       ("shared", "hello"), ("emptied", "abcd"), ("unshared", "w" * 70)):
                out.append(("%s %r <- %s %r" % (dst_kind, dst_text[:6], src_kind, src_text[:6]),
                            dst_kind, dst_text, src_kind, src_text))
    return out


@register("prop.string.TGString_AssignString")
def assign_string_test():
    scenarios = []
    for label, dst_kind, dst_text, src_kind, src_text in two_string_scenarios():
        roots = strings(**{"d": Obj("TGString"), "src": Obj("TGString"), "src2": Obj("TGString")})
        pre = [Pre("ctor", [Root("d"), narrow(dst_text)])] if dst_kind != "none" else []
        pre = pre + build_source(src_kind, src_text)
        scenarios.append(Scenario(roots, [Root("d"), Root("src")], pre=pre, label=label))
    for kind in STATES:
        scenarios.append(scenario(kind, "selfie", [Root("s"), Root("s")], label="self " + kind))
    scenarios += many_sharers_scenarios()
    return case(ASSIGN_STRING, "bc_TGString_AssignString", scenarios, probes=[string_state("s", *HELPERS, "d", "src", "src2")])


def many_sharers_scenarios():
    """A source buffer shared by 253 and by 254 other strings: one more share is still allowed, then not."""
    out = []
    for sharers in (253, 254):
        roots = strings(**{"d": Obj("TGString"), "src": Obj("TGString")})
        roots.update({"n%d" % i: Obj("TGString") for i in range(sharers)})
        pre = [Pre("ctor", [Root("v"), narrow("shared text")]), Pre("assign", [Root("src"), Root("v")])]
        pre += [Pre("assign", [Root("n%d" % i), Root("src")]) for i in range(sharers)]
        out.append(Scenario(roots, [Root("d"), Root("src")], pre=pre, label="source shared by %d strings" % sharers))
    return out


def build_source(kind, text):
    """Setup calls that bring the string src into a state, using the helpers src2 and t."""
    ctor = lambda name, value: Pre("ctor", [Root(name), narrow(value)])
    assign = lambda dst, src: Pre("assign", [Root(dst), Root(src)])
    if kind == "none":
        return []
    if kind == "frozen":
        return [ctor("src", text)]
    if kind == "unshared":
        return [ctor("t", text), assign("src", "t")]
    if kind == "shared":
        return [ctor("t", text), assign("src2", "t"), assign("src", "src2")]
    if kind == "emptied":
        return [ctor("src", text), Pre("narrow", [Root("src"), narrow("")])]
    raise ValueError(kind)


# Compare and CompareC ---------------------------------------------------------------------------------------

WIDE_TEXTS = ["", "a", "A", "abc", "ABC", "abd", "ab", "Hello", "hello", "HELLO", "Z", "a", "[", "{", "@", "`",
              "é", "É", "Ā", "ÿ", "Α", "α", "￿", "😀", "xĀy", "xay"]


def wide_string(text, null_for_empty=False):
    units = [ord(c) if ord(c) < 0x10000 else 0 for c in text]
    if text.startswith("\ud83d"):
        units = [0xD83D, 0xDE00]
    if null_for_empty and not units:
        return Obj("TGString")
    return Obj("TGString", buffer=units + [0], length=len(units), capacity=len(units))


@register("prop.string.TGString_Compare")
def compare_test():
    scenarios = []
    pairs = [(a, b) for a in WIDE_TEXTS for b in WIDE_TEXTS]
    for a, b in pairs:
        for ignore in (0, 1):
            scenarios.append(Scenario({"a": wide_string(a), "b": wide_string(b)}, [Root("a"), Root("b"), ignore],
                                      label="%r %r ignoreCase=%d" % (a, b, ignore)))
    for ignore in (0, 1):
        scenarios.append(Scenario({"a": wide_string("", True), "b": wide_string("x")}, [Root("a"), Root("b"), ignore],
                                  label="null buffer, ignoreCase=%d" % ignore))
        scenarios.append(Scenario({"a": wide_string("x"), "b": wide_string("", True)}, [Root("a"), Root("b"), ignore]))
        scenarios.append(Scenario({"a": wide_string("", True), "b": wide_string("", True)}, [Root("a"), Root("b"), ignore]))
    return case(COMPARE, "bc_TGString_Compare", scenarios, probes=[])


@register("prop.string.TGString_CompareC")
def compare_c_test():
    scenarios = []
    for a in ("", "abc", "Hello", "café", "Ā", "x" * 40):
        for b in ("", "abc", "abd", "ab", "Hello", "hello", "caf\xe9", "\xe9", "x" * 40, "x" * 41):
            scenarios.append(Scenario({"a": wide_string(a)}, [Root("a"), narrow(b), 0], label="%r %r" % (a, b)))
    null_scenarios = [Scenario({"a": wide_string(a)}, [Root("a"), 0, 5], label="%r null" % a) for a in ("", "abc")]
    return [case(COMPARE_C, "bc_TGString_CompareC", scenarios, probes=[]),
            case(COMPARE_C_NULL, "bc_TGString_CompareC", null_scenarios, probes=[])]


# NewNarrowCopy -----------------------------------------------------------------------------------------------

@register("prop.string.TGString_NewNarrowCopy")
def new_narrow_copy_test():
    scenarios = [Scenario({"a": wide_string(t)}, [Root("a")], label=repr(t)) for t in
                 ("", "a", "hello", "café", "Āÿā", "x￿y", "😀", "w" * 100)]
    scenarios.append(Scenario({"a": wide_string("", True)}, [Root("a")], label="null buffer"))
    scenarios += [scenario(kind, "abcdef", [Root("s")], label="constructed " + kind) for kind in STATES]
    scenarios = [s if "a" in s.roots else Scenario(s.roots, [Root("s")], pre=s.pre, label=s.label) for s in scenarios]
    return case(NEW_NARROW_COPY, "bc_TGString_NewNarrowCopy", scenarios, probes=[narrow_text()])


# Helpers of the string template ---------------------------------------------------------------------------------

@register("prop.string.Tidy")
def tidy_test():
    scenarios = [scenario(kind, text, [Root("s"), built], label="%s %r built=%d" % (kind, text[:6], built))
                 for kind in STATES for text in SHORT_TEXTS[:4] for built in (0, 1)]
    return case(TIDY, "bc_TGString_Tidy", scenarios, allow_leaks=True)  # Tidy(false) drops the buffer by design


@register("prop.string.Eos")
def eos_test():
    scenarios = [scenario("frozen", "abcdefgh", [Root("s"), n], label="length %d" % n) for n in (0, 1, 3, 8, 20, 31)]
    scenarios += [scenario("unshared", "k" * 40, [Root("s"), n], label="long, length %d" % n) for n in (0, 40, 63)]
    return case(EOS, "bc_TGString_Eos", scenarios)


@register("prop.string.Grow")
def grow_test():
    sizes = (0, 1, 3, 30, 31, 32, 33, 62, 63, 64, 100, 1000)
    scenarios = [scenario(kind, text, [Root("s"), n, trim], label="%s %r n=%d trim=%d" % (kind, text[:6], n, trim))
                 for kind in STATES for text in ("", "abc", "m" * 40) for n in sizes for trim in (0, 1)]
    for capacity in (1, 30, 31, 32, 40, 47, 48, 100):
        for n in (1, 5, 31, 40, 48, 101):
            for trim in (0, 1):
                scenarios.append(Scenario(strings(s=Obj("TGString", capacity=capacity)), [Root("s"), n, trim],
                                          label="no buffer, capacity %d n=%d trim=%d" % (capacity, n, trim)))
    return case(GROW, "bc_TGString_Grow", scenarios)


@register("prop.string.CopyBuffer")
def copy_buffer_test():
    sizes = (0, 1, 3, 30, 31, 32, 63, 64, 100, 1000)
    scenarios = [scenario(kind, text, [Root("s"), n], label="%s %r n=%d" % (kind, text[:6], n))
                 for kind in STATES for text in ("", "abc", "m" * 40) for n in sizes]
    return case(COPY_BUFFER, "bc_TGString_CopyBuffer", scenarios)


@register("prop.string.Split")
def split_test():
    scenarios = [scenario(kind, text, [Root("s")]) for kind in STATES for text in SHORT_TEXTS]
    return case(SPLIT, "bc_TGString_Split", scenarios)


@register("prop.string.AssignChars")
def assign_chars_test():
    scenarios = []
    for kind in STATES:
        for start in ("", "abc", "m" * 40):
            for chars in ("", "x", "xyz", "Āā", "c" * 31, "c" * 32, "c" * 70):
                scenarios.append(scenario(kind, start, [Root("s"), wide(chars), len(chars)],
                                          label="%s %r <- %r" % (kind, start[:6], chars[:6])))
    for capacity in (5, 31, 40, 47, 48, 100):
        for chars in ("x", "xyz", "c" * 31, "c" * 40, "c" * 48):
            if capacity <= 0x1F and capacity >= len(chars):
                continue  # the exe would write into the missing buffer
            scenarios.append(Scenario(strings(s=Obj("TGString", capacity=capacity)), [Root("s"), wide(chars), len(chars)],
                                      label="no buffer, capacity %d, %d characters" % (capacity, len(chars))))
    return case(ASSIGN_CHARS, "bc_TGString_AssignChars", scenarios, ret_class="TGString")


# Character copy functions and the C run time ---------------------------------------------------------------

@register("prop.string.MoveChars")
def move_chars_test():
    rows = [(wide("z" * 12) + bytes(8), wide("0123456789ab") + bytes(8), n) for n in (0, 1, 2, 5, 12)]
    original = orig(0x40E360, 0x40E3AC, "cdecl", [Buf(size=40), Buf(size=40, io="in"), U32], None)
    return Case(original, Native(LIB, "bc_TGString_MoveChars"), rows, label="separate buffers")


@register("prop.string.CopyChars")
def copy_chars_test():
    rows = [(wide("z" * 12) + bytes(8), wide("0123456789ab") + bytes(8), n) for n in (0, 1, 2, 5, 12)]
    original = orig(0x40E330, 0x40E355, "cdecl", [Buf(size=40), Buf(size=40, io="in"), U32], None)
    return Case(original, Native(LIB, "bc_TGString_CopyChars"), rows)


@register("prop.string.FillChars")
def fill_chars_test():
    rows = [(wide("z" * 12) + bytes(8), v, n) for v in (0, 0x41, 0x1234, 0xFFFF, 0x12345) for n in (0, 1, 2, 3, 12)]
    original = orig(0x6F41A0, 0x6F41D1, "cdecl", [Buf(size=40), U32, U32], None)  # the returned pointer is not compared
    return Case(original, Native(LIB, "bc_TGString_FillChars"), rows)


@register("prop.string.MaxSize")
def max_size_test():
    return Case(orig(0x416DD0, 0x416DD5, "thiscall", [PTR], U32), Native(LIB, "bc_TGString_MaxSize_this"), [(0,)])


@register("prop.crt.wcslen")
def wcslen_test():
    rows = [(wide(t),) for t in WIDE_TEXTS] + [(wide("x" * 100),), (b"\0\0",)]
    return Case(orig(0x859D47, 0x859D63, "cdecl", [Buf(io="in")], U32), Native(LIB, "bc_crt_wcslen"), rows)


@register("prop.crt.wcscmp")
def wcscmp_test():
    rows = [(wide(a), wide(b)) for a in WIDE_TEXTS for b in WIDE_TEXTS]
    return Case(orig(0x85BE82, 0x85BEB6, "cdecl", [Buf(io="in"), Buf(io="in")], I32), Native(LIB, "bc_crt_wcscmp"), rows)


@register("prop.crt.wcsicmp")
def wcsicmp_test():
    rows = [(wide(a), wide(b)) for a in WIDE_TEXTS for b in WIDE_TEXTS]
    return Case(orig(0x86F8E0, 0x86F9BE, "cdecl", [Buf(io="in"), Buf(io="in")], I32), Native(LIB, "bc_crt_wcsicmp"), rows)


# Planted defects: each must be found, and the report must name where -------------------------------------------

@register("prop.string.planted_defect_compare_ignores_the_case_flag")
def planted_compare():
    scenarios = [Scenario({"a": wide_string(a), "b": wide_string(b)}, [Root("a"), Root("b"), 1],
                          label="%r %r ignoreCase=1" % (a, b)) for a, b in (("Hello", "hello"), ("ABC", "abc"))]
    scenarios.append(Scenario({"a": wide_string("same"), "b": wide_string("same")}, [Root("a"), Root("b"), 1],
                              label="equal strings"))
    text = failing_text(case(COMPARE, "bc_TGString_Compare_defect", scenarios, probes=[]))
    assert "2 of 3 scenarios differ" in text and "'Hello' 'hello' ignoreCase=1" in text, text
    assert "ret: original=" in text and "equal strings" not in text, text


@register("prop.string.planted_defect_share_without_count_is_found_by_the_probe_only")
def planted_no_share():
    scenarios = [scenario("unshared", "hello", [Root("s"), Root("t")], label="source with a copy buffer"),
                 scenario("shared", "hello", [Root("s"), Root("u")], label="source with a shared buffer")]
    blind = case(ASSIGN_STRING, "bc_TGString_AssignString_defect", scenarios, probes=[])
    assert all(r.ok for r in blind.run()), "the graph alone should not see the difference: " + failing_text(blind)
    text = failing_text(case(ASSIGN_STRING, "bc_TGString_AssignString_defect", scenarios))
    assert "probe string state of s, t, u, v: original=" in text, text


@register("prop.string.planted_defect_extra_block_is_found_by_the_heap_counts")
def planted_leak():
    scenarios = [scenario("frozen", "abc", [Root("s"), narrow("xyz")], label="frozen <- xyz")]
    text = failing_text(case(ASSIGN_NARROW, "bc_TGString_AssignNarrow_defect", scenarios, probes=[]))
    assert "heap blocks allocated by the call: original=0 native=1" in text, text


# Checks of the harness features that these tests use -------------------------------------------------------------

@register("prop.harness.opaque_pointer_return_null_against_pointer_is_found")
def planted_opaque_return():
    scenarios = [Scenario({"a": wide_string("text")}, [Root("a")], label="text")]
    text = failing_text(case(NEW_NARROW_COPY, "bc_TGString_NewNarrowCopy_defect", scenarios, probes=[]))
    assert "ret: original=a pointer native=null" in text, text


@register("prop.harness.setup_call_result_difference_is_found")
def planted_setup_result():
    calls = dict(SETUP_CALLS)
    calls["compare"] = SetupCall(COMPARE, Native(LIB, "bc_TGString_Compare_defect"))
    scenarios = [Scenario({"s": Obj("TGString"), "t": Obj("TGString")}, [Root("s")],
                          pre=[Pre("ctor", [Root("s"), narrow("Hello")]), Pre("ctor", [Root("t"), narrow("hello")]),
                               Pre("compare", [Root("s"), Root("t"), 1])], label="setup calls with a wrong compare")]
    broken = ObjectCase(MODEL, DTOR, Native(LIB, "bc_TGString_Destroy"), scenarios, setup_calls=calls,
                        stubs=pool_stubs_zeroed())
    text = failing_text(broken)
    assert "setup call #2 compare: ret: original=" in text, text
