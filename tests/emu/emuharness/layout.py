"""Field maps: how a class looks in the exe (32-bit) and how the native side reads it.

A class is a list of members. Each member has the exe offset, the exe type and,
optionally, native accessor functions. Without accessors the native offset comes
from a table that the native library exports (see fixtures/bc_object.h).

    NODE = ClassDef("Node", 0x14, [
        Member("vtbl", 0x00, VTBL),
        Member("name", 0x04, CSTR),
        Member("weight", 0x08, F32),
        Member("next", 0x0C, Ptr("Node")),
    ], vtable=0x8A1000)
    MODEL = Model([NODE], NativeApi(lib))
"""
import ctypes
from dataclasses import dataclass
from typing import Dict, List, Optional, Sequence, Union

from . import types as T
from .errors import HarnessError

EXE_POINTER_SIZE = 4
EXE_WCHAR_SIZE = 2


class _Kind:
    def __init__(self, name: str):
        self.name = name

    def __repr__(self):
        return self.name


VTBL = _Kind("VTBL")
CSTR = _Kind("CSTR")
WSTR = _Kind("WSTR")


@dataclass(frozen=True)
class Ptr:
    """Pointer to an object of the described class."""
    target: str


@dataclass(frozen=True)
class Inline:
    """An object of the described class stored inside the parent."""
    target: str


@dataclass(frozen=True)
class Array:
    """Pointer to an array of scalars or object pointers. count names the member that holds the length."""
    elem: Union[T.Scalar, Ptr]
    count: str


@dataclass(frozen=True)
class Accessor:
    """Exported native functions: get(obj) returns the value, set(obj, value) stores it."""
    get: str
    set: Optional[str] = None


@dataclass(frozen=True)
class Member:
    name: str
    offset: int
    type: object
    native: Optional[Accessor] = None


@dataclass
class ClassDef:
    """size is the exe size. vtable is the exe address of the vtable of this class.

    counted=False leaves the class out of the live-object counts: use it for classes that the
    tested code creates and destroys itself. The heap block counts still cover them.
    """
    name: str
    size: int
    members: Sequence[Member]
    vtable: Optional[int] = None
    base: Optional[str] = None
    native_new: Optional[str] = None
    counted: bool = True

    @property
    def factory(self) -> str:
        return self.native_new or "bc_new_" + self.name


@dataclass
class NativeApi:
    """Names of the functions the native library exports for objects.

    class_name(object) returns the class of an object. With class_with_declared it gets the declared
    class as a second argument, for libraries whose classes have no common base.
    heap_allocations and heap_frees return the number of blocks that native code allocated and freed
    so far; the case compares them with the guest heap over the tested call.
    heap_mark starts the check of the guard bytes: only blocks allocated after the mark are checked.
    heap_overruns(buffer, capacity) writes a line "address size offset" for each block whose guard bytes
    changed and returns the number of lines.
    """
    lib: str
    class_name: str = "bc_class_name"
    live_count: str = "bc_live_count"
    class_with_declared: bool = False
    heap_allocations: Optional[str] = None
    heap_frees: Optional[str] = None
    heap_mark: Optional[str] = None
    heap_overruns: Optional[str] = None


class Model:
    def __init__(self, classes: Sequence[ClassDef], native: Optional[NativeApi] = None):
        self.classes: Dict[str, ClassDef] = {}
        for c in classes:
            if c.name in self.classes:
                raise HarnessError("class %s is described twice" % c.name)
            self.classes[c.name] = c
        self.native = native
        self._members: Dict[str, List[Member]] = {}
        self.by_vtable: Dict[int, str] = {}
        for c in classes:
            if c.vtable is not None:
                if c.vtable in self.by_vtable:
                    raise HarnessError("classes %s and %s share vtable 0x%08x"
                                       % (self.by_vtable[c.vtable], c.name, c.vtable))
                self.by_vtable[c.vtable] = c.name
        for c in classes:
            self._check(c)

    def cls(self, name: str) -> ClassDef:
        if name not in self.classes:
            raise HarnessError("class %r is not in the model" % name)
        return self.classes[name]

    def members(self, name: str) -> List[Member]:
        """Members of the class with the members of its bases first."""
        if name not in self._members:
            c = self.cls(name)
            inherited = self.members(c.base) if c.base else []
            self._members[name] = inherited + list(c.members)
        return self._members[name]

    def member(self, cls: str, name: str) -> Member:
        for m in self.members(cls):
            if m.name == name:
                return m
        raise HarnessError("class %s has no member %r" % (cls, name))

    def vtable_member(self, name: str) -> Optional[Member]:
        for m in self.members(name):
            if m.type is VTBL:
                return m
        return None

    def is_a(self, name: str, base: str) -> bool:
        while name:
            if name == base:
                return True
            name = self.cls(name).base
        return False

    def exe_size(self, mtype) -> int:
        if isinstance(mtype, T.Scalar):
            return mtype.size
        if isinstance(mtype, Inline):
            return self.cls(mtype.target).size
        return EXE_POINTER_SIZE

    def _check(self, c: ClassDef):
        members = self.members(c.name)
        names = [m.name for m in members]
        if len(set(names)) != len(names):
            raise HarnessError("class %s has a member name twice" % c.name)
        if c.base and c.size < self.cls(c.base).size:
            raise HarnessError("class %s is smaller than its base %s" % (c.name, c.base))
        end = 0
        for m in sorted(members, key=lambda m: m.offset):
            if m.offset < end:
                raise HarnessError("class %s: member %s at 0x%x overlaps the member before it"
                                   % (c.name, m.name, m.offset))
            end = m.offset + self.exe_size(m.type)
            self._check_type(c, m)
        if end > c.size:
            raise HarnessError("class %s: members end at 0x%x, after the size 0x%x" % (c.name, end, c.size))
        if c.vtable is not None and self.vtable_member(c.name) is None:
            raise HarnessError("class %s has a vtable address but no VTBL member" % c.name)

    def _check_type(self, c: ClassDef, m: Member):
        t = m.type
        if isinstance(t, Array):
            count = self.member(c.name, t.count)
            if not (isinstance(count.type, T.Scalar) and not count.type.is_float):
                raise HarnessError("class %s: count member %s of %s is not an integer"
                                   % (c.name, t.count, m.name))
            t = t.elem
        if isinstance(t, (Ptr, Inline)):
            self.cls(t.target)


def native_pointer_size() -> int:
    return ctypes.sizeof(ctypes.c_void_p)


def native_wchar_size() -> int:
    return ctypes.sizeof(ctypes.c_wchar)
