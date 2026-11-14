"""The two memories an object graph can live in: the emulated exe and the native library."""
import ctypes
from typing import Dict, List

from unicorn import UcError

from . import types as T
from .errors import HarnessError, UnmappedAccess
from .layout import EXE_POINTER_SIZE, EXE_WCHAR_SIZE, Member, Model, native_pointer_size, native_wchar_size
from .machine import Machine
from .native import load_library
from .objects import Global, Side


class GuestSide(Side):
    """The exe layout in the emulator heap. Blocks come from the tracked guest heap."""

    pointer_size = EXE_POINTER_SIZE
    wchar_size = EXE_WCHAR_SIZE

    def __init__(self, machine: Machine, model: Model):
        self.machine = machine
        self.model = model

    def offset(self, cls: str, member: Member) -> int:
        return member.offset

    def new_object(self, cls: str) -> int:
        size = self.model.cls(cls).size
        addr = self.machine.heap.new_block(size, cls)
        self.write(addr, bytes(size))
        return addr

    def new_blob(self, data: bytes) -> int:
        addr = self.machine.heap.new_block(max(len(data), 1))
        self.write(addr, data)
        return addr

    def new_global(self, glob: Global) -> int:
        return glob.address

    def write(self, addr: int, data: bytes):
        self.machine.write(addr, data)

    def read(self, addr: int, size: int) -> bytes:
        try:
            return self.machine.read(addr, size)
        except UcError:
            raise UnmappedAccess("object read at unmapped address 0x%08x" % addr)

    def put_vtable(self, addr: int, cls: str, member: Member):
        vtable = self.model.cls(cls).vtable
        if vtable is None:
            raise HarnessError("class %s has no exe vtable address" % cls)
        self.write(addr + member.offset, vtable.to_bytes(4, "little"))

    def dynamic_class(self, addr: int, declared: str) -> str:
        offset = self.model.vtable_member(declared).offset
        vtable = int.from_bytes(self.read(addr + offset, 4), "little")
        return self.model.by_vtable.get(vtable, "<unknown vtable 0x%08x>" % vtable)


class _BcField(ctypes.Structure):
    _fields_ = [("name", ctypes.c_char_p), ("offset", ctypes.c_uint32)]


class NativeSide(Side):
    """Native objects made by the exported factories. Python keeps the memory of strings and arrays."""

    pointer_size = native_pointer_size()
    wchar_size = native_wchar_size()

    def __init__(self, model: Model):
        if model.native is None:
            raise HarnessError("the model has no native library")
        self.model = model
        self.lib = load_library(model.native.lib)
        self.keep: List = []
        self.owners: Dict[int, str] = {}
        self._tables: Dict[str, Dict[str, int]] = {}
        if model.native.heap_mark is not None:
            self._function(model.native.heap_mark, None, [])()

    def _function(self, name: str, restype, argtypes):
        fn = getattr(self.lib, name)
        fn.restype, fn.argtypes = restype, argtypes
        return fn

    def _table(self, cls: str) -> Dict[str, int]:
        if cls not in self._tables:
            symbol = "bc_fields_" + cls
            try:
                first = ctypes.addressof(ctypes.c_char.in_dll(self.lib, symbol))
            except ValueError:
                raise HarnessError("the native library exports no field table %s" % symbol)
            rows = ctypes.cast(first, ctypes.POINTER(_BcField))
            table, i = {}, 0
            while rows[i].name is not None:
                table[rows[i].name.decode()] = rows[i].offset
                i += 1
            self._tables[cls] = table
        return self._tables[cls]

    def offset(self, cls: str, member: Member) -> int:
        table = self._table(cls)
        if member.name not in table:
            raise HarnessError("native field table of %s has no entry for %r" % (cls, member.name))
        return table[member.name]

    def new_object(self, cls: str) -> int:
        factory = self.model.cls(cls).factory
        if not hasattr(self.lib, factory):
            raise HarnessError("the native library exports no factory %s for class %s" % (factory, cls))
        addr = self._function(factory, ctypes.c_void_p, [])()
        if not addr:
            raise HarnessError("native factory %s returned null" % factory)
        self.owners[addr] = cls
        return addr

    def new_blob(self, data: bytes) -> int:
        buf = ctypes.create_string_buffer(data, max(len(data), 1))
        self.keep.append(buf)
        return ctypes.addressof(buf)

    def new_global(self, glob: Global) -> int:
        addr = self._function(glob.native_init, ctypes.c_void_p, [])()
        if not addr:
            raise HarnessError("native function %s returned null" % glob.native_init)
        return addr

    def write(self, addr: int, data: bytes):
        ctypes.memmove(addr, data, len(data))

    def read(self, addr: int, size: int) -> bytes:
        return ctypes.string_at(addr, size)

    def put_vtable(self, addr: int, cls: str, member: Member):
        pass

    def dynamic_class(self, addr: int, declared: str) -> str:
        api = self.model.native
        if api.class_with_declared:
            fn = self._function(api.class_name, ctypes.c_char_p, [ctypes.c_void_p, ctypes.c_char_p])
            name = fn(addr, declared.encode())
        else:
            name = self._function(api.class_name, ctypes.c_char_p, [ctypes.c_void_p])(addr)
        return name.decode() if name else "<native class name is null>"

    @staticmethod
    def _ctype(member: Member):
        return member.type.ctype if isinstance(member.type, T.Scalar) else ctypes.c_void_p

    def put(self, addr: int, cls: str, member: Member, value):
        accessor = member.native
        if accessor is None:
            return super().put(addr, cls, member, value)
        if accessor.set is None:
            raise HarnessError("accessor of %s.%s has no setter" % (cls, member.name))
        arg = T.native_value(member.type, value) if isinstance(member.type, T.Scalar) else value
        self._function(accessor.set, None, [ctypes.c_void_p, self._ctype(member)])(addr, arg)

    def fetch(self, addr: int, cls: str, member: Member):
        accessor = member.native
        if accessor is None:
            return super().fetch(addr, cls, member)
        value = self._function(accessor.get, self._ctype(member), [ctypes.c_void_p])(addr)
        if isinstance(member.type, T.Scalar):
            return T.encode(member.type, value)
        return value or 0

    def live_counts(self) -> Dict[str, int]:
        """Live objects per class, for the classes that have a native factory."""
        count = self._function(self.model.native.live_count, ctypes.c_int64, [ctypes.c_char_p])
        return {name: count(name.encode()) for name, c in self.model.classes.items()
                if c.counted and hasattr(self.lib, c.factory)}

    def heap_counts(self):
        """(blocks allocated, blocks freed) so far, or None when the library has no counters."""
        api = self.model.native
        if api.heap_allocations is None:
            return None
        return (self._function(api.heap_allocations, ctypes.c_int64, [])(),
                self._function(api.heap_frees, ctypes.c_int64, [])())

    def heap_overruns(self, owners: Dict[int, str] = None) -> List[str]:
        """One text for each native block whose guard bytes changed. owners maps block addresses to classes."""
        api = self.model.native
        if api.heap_overruns is None:
            return []
        known = dict(self.owners)
        known.update(owners or {})
        buffer = ctypes.create_string_buffer(1 << 16)
        count = self._function(api.heap_overruns, ctypes.c_int, [ctypes.c_char_p, ctypes.c_int])(buffer, len(buffer))
        if count < 0:
            raise HarnessError("the list of overrun blocks does not fit into the buffer")
        found = []
        for line in buffer.value.decode().splitlines():
            address, size, offset = (int(v) for v in line.split())
            cls = known.get(address)
            where = ("%d past the end" % (offset - size + 1)) if offset >= size else "before the start"
            found.append("heap block overrun: block of %d bytes at 0x%x%s: the byte at offset %d (%s) was changed"
                         % (size, address, " (" + cls + ")" if cls else "", offset, where))
        return found
