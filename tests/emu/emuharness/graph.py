"""Object graphs read from memory, and their comparison.

A pointer to an object is a GObj. One memory object is one GObj, so two pointers to
the same object give the same GObj. The comparison checks that the identity pattern is
equal on both sides: it does not compare addresses.
"""
from collections import deque
from dataclasses import dataclass
from typing import Dict, List, Optional

from . import types as T


class GObj:
    def __init__(self, cls: str, addr: int):
        self.cls = cls
        self.addr = addr
        self.fields: Dict[str, object] = {}


@dataclass(frozen=True)
class Num:
    scalar: T.Scalar
    raw: bytes

    def text(self) -> str:
        value = T.decode(self.scalar, self.raw)
        if self.scalar.is_float:
            return "%r [0x%s]" % (value, self.raw[::-1].hex())
        return repr(value)


@dataclass(frozen=True)
class Embedded:
    """An object stored inside its parent. It has no identity of its own."""
    obj: GObj


def describe(value) -> str:
    if value is None:
        return "null"
    if isinstance(value, Num):
        return value.text()
    if isinstance(value, str):
        return '"%s"' % value
    if isinstance(value, (GObj, Embedded)):
        return "a %s object" % (value.obj.cls if isinstance(value, Embedded) else value.cls)
    if isinstance(value, list):
        return "an array of %d" % len(value)
    return repr(value)


class _Comparison:
    def __init__(self):
        self.queue = deque()
        self.original_seen: Dict[int, tuple] = {}
        self.native_seen: Dict[int, tuple] = {}
        self.diffs: List[str] = []

    def diff(self, path: str, text: str):
        self.diffs.append("%s: %s" % (path, text))

    def pointer(self, a: Optional[GObj], b: Optional[GObj], path: str):
        if a is None and b is None:
            return
        if a is None or b is None:
            self.diff(path, "original=%s native=%s" % (describe(a), describe(b)))
            return
        known_a, known_b = self.original_seen.get(id(a)), self.native_seen.get(id(b))
        if known_a is None and known_b is None:
            self.original_seen[id(a)] = (b, path)
            self.native_seen[id(b)] = (a, path)
            self.queue.append((a, b, path))
        elif known_a is not None and known_a[0] is b:
            return
        elif known_a is not None:
            self.diff(path, "original points to the same object as %s, native points to another object"
                      % known_a[1])
        else:
            self.diff(path, "native points to the same object as %s, original points to another object"
                      % known_b[1])

    def value(self, a, b, path: str):
        if isinstance(a, (GObj, type(None))) and isinstance(b, (GObj, type(None))):
            self.pointer(a, b, path)
        elif isinstance(a, Embedded) and isinstance(b, Embedded):
            self.queue.append((a.obj, b.obj, path))
        elif isinstance(a, list) and isinstance(b, list):
            if len(a) != len(b):
                self.diff(path, "original has %d elements, native has %d" % (len(a), len(b)))
            for i, (x, y) in enumerate(zip(a, b)):
                self.value(x, y, "%s[%d]" % (path, i))
        elif a != b:
            self.diff(path, "original=%s native=%s" % (describe(a), describe(b)))

    def object(self, a: GObj, b: GObj, path: str):
        if a.cls != b.cls:
            self.diff(path, "class original=%s native=%s" % (a.cls, b.cls))
            return
        for name, va in a.fields.items():
            self.value(va, b.fields.get(name), "%s.%s" % (path, name))

    def run(self, original: Dict[str, Optional[GObj]], native: Dict[str, Optional[GObj]]) -> List[str]:
        for name in dict.fromkeys(list(original) + list(native)):
            self.pointer(original.get(name), native.get(name), name)
        while self.queue:
            self.object(*self.queue.popleft())
        return self.diffs


def compare_graphs(original: Dict[str, Optional[GObj]], native: Dict[str, Optional[GObj]]) -> List[str]:
    """Return one line per difference, with the path from the root, for example `list.next.name`.

    Compared: class, every member of the field map, and the identity pattern of pointers.
    Not compared: addresses, members that are not in the field map.
    """
    return _Comparison().run(original, native)
