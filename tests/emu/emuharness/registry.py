"""Test registration and execution."""
import time
import traceback
from contextlib import contextmanager
from dataclasses import dataclass
from typing import Callable, Dict, List

from . import inputs
from .case import NOT_COMPARED, Case, run_case
from .errors import HarnessError

PASS, FAIL, XFAIL, XPASS, ERROR = "PASS", "FAIL", "XFAIL", "XPASS", "ERROR"


@dataclass
class Entry:
    name: str
    fn: Callable
    expect_fail: bool


@dataclass
class Outcome:
    name: str
    status: str
    seconds: float
    lines: List[str]

    @property
    def red(self) -> bool:
        return self.status in (FAIL, XPASS, ERROR)


REGISTRY: Dict[str, Entry] = {}


def register(name: str, expect_fail: bool = False):
    """Register a test. The function returns a Case, a list of Cases, or None.

    A function that returns None checks things itself and raises AssertionError on failure.
    With expect_fail the test is green only if it fails; if it passes the run is red.
    """
    def decorate(fn):
        if name in REGISTRY:
            raise ValueError("duplicate test name %r" % name)
        REGISTRY[name] = Entry(name, fn, expect_fail)
        return fn
    return decorate


@contextmanager
def expect_error(cls, *fragments):
    """Check that the block raises cls and that the message holds every fragment."""
    box = []
    try:
        yield box
    except cls as exc:
        box.append(exc)
        for fragment in fragments:
            assert fragment in str(exc), "error message %r lacks %r" % (str(exc), fragment)
        return
    raise AssertionError("expected %s, but nothing was raised" % cls.__name__)


def _execute(entry: Entry) -> (bool, List[str]):
    """Return (passed, lines)."""
    lines = []
    try:
        result = entry.fn()
        if result is None:
            return True, lines
        cases = result if isinstance(result, (list, tuple)) else [result]
        passed = True
        if not all(hasattr(case, "run") for case in cases):
            lines.append("  not compared: " + NOT_COMPARED)
        for case in cases:
            for res in (case.run() if hasattr(case, "run") else run_case(case)):
                passed = passed and res.ok
                lines += res.lines
        return passed, lines
    except AssertionError as exc:
        return False, ["  assertion failed: %s" % exc]
    except HarnessError as exc:
        return False, ["  %s: %s" % (type(exc).__name__, exc)]


def run_entry(entry: Entry) -> Outcome:
    start = time.time()
    inputs.take_log()
    try:
        passed, lines = _execute(entry)
        lines = ["  " + m for m in inputs.take_log()] + lines
    except Exception:
        return Outcome(entry.name, ERROR, time.time() - start, traceback.format_exc().splitlines())
    if entry.expect_fail:
        status = XFAIL if not passed else XPASS
        if status == XPASS:
            lines = ["  expected failure did not happen: the checker missed a known defect"] + lines
    else:
        status = PASS if passed else FAIL
    return Outcome(entry.name, status, time.time() - start, lines)
