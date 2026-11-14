"""Describe an object graph in Python, build it in memory and read it back.

The same description builds the guest (exe layout) and the native side. A pointer
member holds an Obj, a Global or None. Two members that hold the same Python object
point to the same memory object.

    node2 = Obj("Node", name="b", tag=2)
    node1 = Obj("Node", name="a", tag=1, next=node2)
"""
from collections import deque
from dataclasses import dataclass
from typing import Dict, List, Optional, Sequence, Tuple

from . import types as T
from .errors import HarnessError
from .graph import Embedded, GObj, Num
from .layout import CSTR, VTBL, WSTR, Array, Inline, Member, Model, Ptr

MAX_STRING = 1 << 16
MAX_ARRAY = 1 << 20


class Obj:
    """An object of a described class. Members that are not given stay zero."""

    def __init__(self, cls: str, **values):
        self.cls = cls
        self.values = values


@dataclass(eq=False)
class Global:
    """An object at a fixed exe address. native_init is the native function that makes
    the matching native object again and returns its address."""
    name: str
    address: int
    obj: Obj
    native_init: str


@dataclass(frozen=True)
class Root:
    """An argument value: the address of the named root or global object."""
    name: str


@dataclass(frozen=True)
class Inner:
    """An argument value: the address of a member that is stored inside the named root, for example the table that a
    group holds inline. The offset of the member comes from the field map of the side."""
    root: str
    member: str

    @property
    def key(self) -> str:
        return "%s.%s" % (self.root, self.member)


@dataclass
class Built:
    addr: Dict[str, int]
    classes: Dict[str, str]

    def roots(self) -> Dict[str, Tuple[int, str]]:
        return {name: (a, self.classes[name]) for name, a in self.addr.items()}


class Side:
    """Memory of one side. The builder and the reader use only these methods."""
    model: Model
    pointer_size: int
    wchar_size: int

    def offset(self, cls: str, member: Member) -> int: ...
    def new_object(self, cls: str) -> int: ...
    def new_blob(self, data: bytes) -> int: ...
    def new_global(self, glob: Global) -> int: ...
    def write(self, addr: int, data: bytes): ...
    def read(self, addr: int, size: int) -> bytes: ...
    def put_vtable(self, addr: int, cls: str, member: Member): ...
    def dynamic_class(self, addr: int, declared: str) -> str: ...

    def put(self, addr: int, cls: str, member: Member, value):
        """Store a number or an address in a member."""
        if isinstance(member.type, T.Scalar):
            data = T.encode(member.type, value)
        else:
            data = value.to_bytes(self.pointer_size, "little")
        self.write(addr + self.offset(cls, member), data)

    def fetch(self, addr: int, cls: str, member: Member):
        """Return the bytes of a scalar member, or the address held by a pointer member."""
        size = member.type.size if isinstance(member.type, T.Scalar) else self.pointer_size
        data = self.read(addr + self.offset(cls, member), size)
        return data if isinstance(member.type, T.Scalar) else int.from_bytes(data, "little")


class ObjectBuilder:
    def __init__(self, side: Side):
        self.side = side
        self.model = side.model
        self.placed: Dict[int, int] = {}

    def build(self, roots: Dict[str, Optional[Obj]], globals: Sequence[Global] = ()) -> Built:
        items = list(globals) + [o for o in roots.values() if o is not None]
        order, stack = [], list(items)
        while stack:
            item = stack.pop()
            if id(item) in self.placed:
                continue
            self.placed[id(item)] = self._place(item)
            order.append(item)
            stack += self._children(_obj(item))
        for item in order:
            self._fill(_obj(item), self.placed[id(item)])
        addr = {g.name: self.placed[id(g)] for g in globals}
        classes = {g.name: g.obj.cls for g in globals}
        for name, obj in roots.items():
            addr[name] = 0 if obj is None else self.placed[id(obj)]
            classes[name] = obj.cls if obj is not None else ""
        return Built(addr, classes)

    def _place(self, item) -> int:
        if isinstance(item, Global):
            return self.side.new_global(item)
        return self.side.new_object(item.cls)

    def _children(self, obj: Obj) -> List:
        found = []
        for m in self.model.members(obj.cls):
            value = obj.values.get(m.name)
            if isinstance(m.type, Ptr):
                found += self._pointees(obj, m, [value], m.type)
            elif isinstance(m.type, Array) and isinstance(m.type.elem, Ptr):
                found += self._pointees(obj, m, value or [], m.type.elem)
            elif isinstance(m.type, Inline) and value is not None:
                found += self._children(value)
        return found

    def _pointees(self, obj: Obj, m: Member, values, ptr: Ptr) -> List:
        found = []
        for v in values:
            if v is None:
                continue
            if not isinstance(v, (Obj, Global)):
                raise HarnessError("%s.%s: expected an Obj, a Global or None, got %r" % (obj.cls, m.name, v))
            if not self.model.is_a(_obj(v).cls, ptr.target):
                raise HarnessError("%s.%s points to %s, but the field map says %s"
                                   % (obj.cls, m.name, _obj(v).cls, ptr.target))
            found.append(v)
        return found

    def _fill(self, obj: Obj, base: int):
        members = self.model.members(obj.cls)
        unknown = set(obj.values) - {m.name for m in members}
        if unknown:
            raise HarnessError("class %s has no member %s" % (obj.cls, ", ".join(sorted(unknown))))
        values = dict(obj.values)
        for m in members:
            if isinstance(m.type, Array) and m.name in values and m.type.count not in values:
                values[m.type.count] = len(values[m.name] or [])
        for m in members:
            if m.type is VTBL:
                self.side.put_vtable(base, obj.cls, m)
            elif m.name in values:
                self._put(obj, base, m, values[m.name])

    def _put(self, obj: Obj, base: int, m: Member, value):
        side, t = self.side, m.type
        if isinstance(t, T.Scalar):
            side.put(base, obj.cls, m, value)
        elif isinstance(t, Ptr):
            side.put(base, obj.cls, m, self._address(value))
        elif t is CSTR:
            side.put(base, obj.cls, m, 0 if value is None else side.new_blob(value.encode("latin-1") + b"\0"))
        elif t is WSTR:
            side.put(base, obj.cls, m, 0 if value is None else side.new_blob(self._wide(value)))
        elif isinstance(t, Array):
            side.put(base, obj.cls, m, 0 if value is None else side.new_blob(self._elements(t, value)))
        elif isinstance(t, Inline):
            self._fill(value, base + side.offset(obj.cls, m))

    def _address(self, value) -> int:
        return 0 if value is None else self.placed[id(value)]

    def _wide(self, text: str) -> bytes:
        encoding = "utf-16-le" if self.side.wchar_size == 2 else "utf-32-le"
        return text.encode(encoding) + bytes(self.side.wchar_size)

    def _elements(self, t: Array, values) -> bytes:
        if isinstance(t.elem, Ptr):
            return b"".join(self._address(v).to_bytes(self.side.pointer_size, "little") for v in values)
        return b"".join(T.encode(t.elem, v) for v in values)


def _obj(item) -> Obj:
    return item.obj if isinstance(item, Global) else item


class GraphReader:
    """Read the graph below the given roots. Each memory object becomes one GObj, so cycles end."""

    def __init__(self, side: Side):
        self.side = side
        self.model = side.model
        self.objects: Dict[Tuple[int, str], GObj] = {}
        self.visited = set()
        self.queue = deque()

    def read(self, roots: Dict[str, Tuple[int, str]]) -> Dict[str, Optional[GObj]]:
        graph = {name: self._pointer(addr, cls) for name, (addr, cls) in roots.items()}
        while self.queue:
            self._fill(self.queue.popleft())
        return graph

    def _pointer(self, addr: int, declared: str) -> Optional[GObj]:
        if addr == 0:
            return None
        self.visited.add(addr)
        cls = declared
        if self.model.vtable_member(declared) is not None:
            cls = self.side.dynamic_class(addr, declared)
        obj = self.objects.get((addr, cls))
        if obj is None:
            obj = self.objects[(addr, cls)] = GObj(cls, addr)
            if cls in self.model.classes:
                self.queue.append(obj)
        return obj

    def _fill(self, obj: GObj):
        members = [m for m in self.model.members(obj.cls) if m.type is not VTBL]
        values = {}
        for m in members:
            if not isinstance(m.type, Array):
                values[m.name] = self._member(obj, m, values)
        for m in members:
            if isinstance(m.type, Array):
                values[m.name] = self._member(obj, m, values)
        obj.fields = {m.name: values[m.name] for m in members}

    def _member(self, obj: GObj, m: Member, values: Dict[str, object]):
        t = m.type
        if isinstance(t, Inline):
            inner = GObj(t.target, obj.addr + self.side.offset(obj.cls, m))
            self._fill(inner)
            return Embedded(inner)
        raw = self.side.fetch(obj.addr, obj.cls, m)
        if isinstance(t, T.Scalar):
            return Num(t, raw)
        if raw == 0:
            return None
        self.visited.add(raw)
        if isinstance(t, Ptr):
            return self._pointer(raw, t.target)
        if t is CSTR:
            return self._string(raw, 1, "latin-1")
        if t is WSTR:
            return self._string(raw, self.side.wchar_size,
                                "utf-16-le" if self.side.wchar_size == 2 else "utf-32-le")
        return self._array(t, raw, T.decode(values[t.count].scalar, values[t.count].raw))

    def _string(self, addr: int, unit: int, encoding: str) -> str:
        data = bytearray()
        while len(data) < MAX_STRING:
            char = self.side.read(addr + len(data), unit)
            if not any(char):
                return bytes(data).decode(encoding, "replace")
            data += char
        raise HarnessError("string at 0x%x has no end within %d bytes" % (addr, MAX_STRING))

    def _array(self, t: Array, addr: int, count: int) -> list:
        if not 0 <= count <= MAX_ARRAY:
            raise HarnessError("array at 0x%x has the length %d" % (addr, count))
        if isinstance(t.elem, Ptr):
            size = self.side.pointer_size
            return [self._pointer(int.from_bytes(self.side.read(addr + i * size, size), "little"),
                                  t.elem.target) for i in range(count)]
        return [Num(t.elem, self.side.read(addr + i * t.elem.size, t.elem.size)) for i in range(count)]
