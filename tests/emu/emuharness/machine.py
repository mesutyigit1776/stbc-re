"""Run single functions of a 32-bit PE image in Unicorn."""
import struct
from dataclasses import dataclass
from typing import Dict, List, Optional, Sequence, Tuple, Union

from unicorn import (UC_ARCH_X86, UC_HOOK_BLOCK, UC_HOOK_CODE, UC_HOOK_MEM_WRITE, UC_MODE_32,
                     UC_PROT_ALL, UC_PROT_EXEC, UC_PROT_READ, UC_PROT_WRITE, Uc, UcError)
from unicorn import unicorn_const as uc_const
from unicorn import x86_const as x86

from . import types as T
from .errors import (HarnessError, RunawayExecution, UnallowedVirtualCall, UnmappedAccess,
                     UnstubbedTarget)
from .pe import PAGE, PEImage, parse_pe
from .stubs import HEAP_BASE, HEAP_SIZE, Heap, Stub, StubContext

STACK_BASE = 0x10000000
STACK_SIZE = 0x00100000
SCRATCH = 0x30000000
THUNK_BASE = 0x7F000000
THUNK_STRIDE = 16
TIB_BASE = 0x7FFDF000
PEB_BASE = 0x7FFDE000
GDT_BASE = 0x7FFD0000
MAGIC_SAVED = {"ebx": 0xB1B1B1B1, "esi": 0x51515151, "edi": 0xD1D1D1D1, "ebp": 0xBBBBBBBB}

DEFAULT_CW = 0x027F
CW_24BIT = 0x007F

CONVENTIONS = ("cdecl", "stdcall", "thiscall", "fastcall")

REGS = {name: getattr(x86, "UC_X86_REG_" + name.upper())
        for name in ("eax", "ebx", "ecx", "edx", "esi", "edi", "ebp", "esp", "eip", "eflags")}

DATA_OFFSET = 0x100
CW_SLOT = SCRATCH + DATA_OFFSET
TARGET_SLOT = SCRATCH + DATA_OFFSET + 4
X87_SLOTS = SCRATCH + DATA_OFFSET + 0x10
RET80 = SCRATCH + DATA_OFFSET + 0x60
ENV_AREA = SCRATCH + DATA_OFFSET + 0x80


@dataclass
class Original:
    """A function of the exe: where it is, how big it is, and how to call it."""
    addr: int
    size: int
    conv: str = "cdecl"
    args: Sequence = ()
    ret: object = T.VOID
    allow: Sequence[Tuple[int, int]] = ()

    def __post_init__(self):
        if self.conv not in CONVENTIONS:
            raise ValueError("unknown calling convention %r" % self.conv)


@dataclass
class CallOutput:
    eax: int
    edx: int
    st0_raw: Optional[bytes]
    buffers: Dict[int, bytes]
    fpu_tags: int
    cw: int


class Machine:
    def __init__(self, exe_path: str, stubs: Dict[Union[int, str], Stub] = None,
                 max_insns: int = 5_000_000):
        with open(exe_path, "rb") as f:
            self.image: PEImage = parse_pe(f.read())
        self.max_insns = max_insns
        self.heap = Heap()
        self.vtables: Dict[int, str] = {}
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        self.allowed: List[Tuple[int, int]] = []
        self.stub_at: Dict[int, Stub] = {}
        self.thunk_names: Dict[int, str] = {}
        self._code_hooks: set = set()
        self._variants: Dict[tuple, int] = {}
        self._error: Optional[Exception] = None
        self._prev_block = (0, 0)
        self._dirty = False
        self._writable: List[Tuple[int, bytes]] = []
        self._pending_prot: List[Tuple[int, object]] = []
        self._variant_end: Dict[int, int] = {}
        self._map_image()
        self._map_support()
        self.heap.bind(self.write, self.read)
        self._bind_imports()
        self._snapshot_writable()
        self.uc.hook_add(UC_HOOK_BLOCK, self._on_block)
        for key, stub in (stubs or {}).items():
            self.add_stub(key, stub)

    def _map_image(self):
        base = self.image.image_base
        self.uc.mem_map(base, PAGE, UC_PROT_READ)
        self.uc.mem_write(base, self.image.data[:PAGE])
        for s in self.image.sections:
            addr = base + s.va
            self.uc.mem_map(addr, s.mapped_size, UC_PROT_ALL)
            self.uc.mem_write(addr, self.image.section_bytes(s))
            self._pending_prot.append((addr, s))

    def _map_support(self):
        uc = self.uc
        uc.mem_map(STACK_BASE, STACK_SIZE, UC_PROT_READ | UC_PROT_WRITE)
        uc.mem_map(HEAP_BASE, HEAP_SIZE, UC_PROT_READ | UC_PROT_WRITE)
        uc.mem_map(SCRATCH, PAGE * 16, UC_PROT_ALL)
        for addr in (TIB_BASE, PEB_BASE, GDT_BASE):
            uc.mem_map(addr, PAGE, UC_PROT_READ | UC_PROT_WRITE)
        stack_top = STACK_BASE + STACK_SIZE
        tib = struct.pack("<IIIIIII", 0xFFFFFFFF, stack_top, STACK_BASE, 0, 0, 0, 0)
        uc.mem_write(TIB_BASE, tib)
        uc.mem_write(TIB_BASE + 0x18, struct.pack("<I", TIB_BASE))
        uc.mem_write(TIB_BASE + 0x30, struct.pack("<I", PEB_BASE))
        self._install_fs()

    def _install_fs(self):
        def descriptor(base, limit, access, flags):
            return struct.pack("<HHBBBB", limit & 0xFFFF, base & 0xFFFF, (base >> 16) & 0xFF,
                               access, (flags << 4) | ((limit >> 16) & 0xF), (base >> 24) & 0xFF)
        gdt = (bytes(8)
               + descriptor(TIB_BASE, 0xFFF, 0x93, 0x4)
               + descriptor(0, 0xFFFFF, 0x93, 0xC)
               + descriptor(0, 0xFFFFF, 0x9B, 0xC))
        self.uc.mem_write(GDT_BASE, gdt)
        self.uc.reg_write(x86.UC_X86_REG_GDTR, (0, GDT_BASE, len(gdt) - 1, 0))
        flat_data = 2 << 3
        for reg in (x86.UC_X86_REG_DS, x86.UC_X86_REG_ES, x86.UC_X86_REG_SS):
            self.uc.reg_write(reg, flat_data)
        self.uc.reg_write(x86.UC_X86_REG_CS, 3 << 3)
        self.uc.reg_write(x86.UC_X86_REG_FS, 1 << 3)

    def _bind_imports(self):
        n = len(self.image.imports)
        if not n:
            return
        size = (n * THUNK_STRIDE + PAGE - 1) & ~(PAGE - 1)
        self.uc.mem_map(THUNK_BASE, size, UC_PROT_READ | UC_PROT_EXEC)
        self.uc.mem_write(THUNK_BASE, b"\xCC" * (n * THUNK_STRIDE))
        self._thunk_limit = THUNK_BASE + n * THUNK_STRIDE
        self.import_addr: Dict[str, int] = {}
        for i, imp in enumerate(self.image.imports):
            thunk = THUNK_BASE + i * THUNK_STRIDE
            self.thunk_names[thunk] = imp.label
            self.import_addr[imp.label.lower()] = thunk
            self.uc.mem_write(self.image.image_base + imp.iat_va, struct.pack("<I", thunk))

    def _snapshot_writable(self):
        for addr, s in self._pending_prot:
            perms = UC_PROT_READ
            if s.writable:
                perms |= UC_PROT_WRITE
            if s.executable:
                perms |= UC_PROT_EXEC
            self.uc.mem_protect(addr, s.mapped_size, perms)
            if s.writable:
                self._writable.append((addr, bytes(self.uc.mem_read(addr, s.mapped_size))))
                self.uc.hook_add(UC_HOOK_MEM_WRITE, self._on_data_write,
                                 begin=addr, end=addr + s.mapped_size - 1)
        del self._pending_prot

    def _restore_writable(self):
        if self._dirty:
            for addr, data in self._writable:
                self.uc.mem_write(addr, data)
            self._dirty = False

    def _on_data_write(self, uc, access, address, size, value, user):
        self._dirty = True

    def read_reg(self, name: str) -> int:
        return self.uc.reg_read(REGS[name])

    def read(self, addr: int, size: int) -> bytes:
        return bytes(self.uc.mem_read(addr, size))

    def write(self, addr: int, data: bytes):
        self.uc.mem_write(addr, data)
        if self.image.image_base <= addr < self.image.image_base + self.image.size_of_image:
            self._dirty = True

    def reset_state(self):
        """Restore the writable sections of the exe and empty the guest heap."""
        self._restore_writable()
        self.heap.reset()

    def read_u32(self, addr: int) -> int:
        return struct.unpack("<I", self.read(addr, 4))[0]

    def add_stub(self, key: Union[int, str], stub: Stub):
        addr = self.resolve(key)
        self.stub_at[addr] = stub
        if addr not in self._code_hooks:
            self._code_hooks.add(addr)
            self.uc.hook_add(UC_HOOK_CODE, self._on_stub, begin=addr, end=addr)

    def resolve(self, key: Union[int, str]) -> int:
        if isinstance(key, int):
            return key
        label = key.lower()
        if label in getattr(self, "import_addr", {}):
            return self.import_addr[label]
        matches = [a for l, a in getattr(self, "import_addr", {}).items() if l.endswith("!" + label)]
        if len(matches) == 1:
            return matches[0]
        raise KeyError("no unique import named %r" % key)

    def _on_stub(self, uc, address, size, user):
        stub = self.stub_at.get(address)
        if stub is None:
            return
        ctx = StubContext(self, stub.conv)
        try:
            stub.fn(ctx)
        except Exception as exc:
            self._fail(HarnessError("stub for 0x%08x raised %r" % (address, exc)))
            return
        esp = self.read_reg("esp")
        ret = self.read_u32(esp)
        uc.reg_write(REGS["eax"], ctx.eax)
        if ctx.edx is not None:
            uc.reg_write(REGS["edx"], ctx.edx)
        uc.reg_write(REGS["esp"], (esp + 4 + stub.cleanup) & 0xFFFFFFFF)
        uc.reg_write(REGS["eip"], ret)

    def _fail(self, exc: Exception):
        if self._error is None:
            self._error = exc
        self.uc.emu_stop()

    def _in_allowed(self, address: int) -> bool:
        for start, size in self.allowed:
            if start <= address < start + size:
                return True
        return False

    def _on_block(self, uc, address, size, user):
        prev = self._prev_block
        self._prev_block = (address, size)
        if address in self.stub_at or self._in_allowed(address):
            return
        if SCRATCH <= address < SCRATCH + PAGE * 16:
            return
        if self._is_import_jump(address):
            return
        description = self._describe_unstubbed(address, prev)
        slots = self._vtable_slots_for(address)
        if slots:
            self._fail(UnallowedVirtualCall("virtual call to unallowed target 0x%08x: %s; %s"
                                            % (address, "; ".join(slots), description)))
            return
        self._fail(UnstubbedTarget(description))

    def _vtable_slots(self, vtable: int) -> List[int]:
        slots = []
        while len(slots) < 512:
            try:
                target = self.read_u32(vtable + 4 * len(slots))
            except UcError:
                break
            section = self.image.section_at(target)
            if section is None or not section.executable:
                break
            slots.append(target)
        return slots

    def _vtable_slots_for(self, target: int) -> List[str]:
        """Name the class and slot of every registered vtable that holds target.

        The vtable of the object in ecx is tried first; ecx is the this pointer of the call.
        """
        this_vtable = None
        try:
            this_vtable = self.read_u32(self.read_reg("ecx"))
        except UcError:
            pass
        order = sorted(self.vtables, key=lambda v: v != this_vtable)
        found = ["class %s, slot %d (vtable 0x%08x)" % (self.vtables[v], i, v)
                 for v in order for i, t in enumerate(self._vtable_slots(v)) if t == target]
        if this_vtable in self.vtables and found and "vtable 0x%08x" % this_vtable in found[0]:
            return found[:1]
        return found

    def _is_import_jump(self, address: int) -> bool:
        """True for the `jmp dword ptr [iat]` thunks that MSVC places in .text."""
        section = self.image.section_at(address)
        if section is None or not section.executable:
            return False
        code = self.read(address, 6)
        if code[:2] != b"\xFF\x25":
            return False
        slot = struct.unpack_from("<I", code, 2)[0] - self.image.image_base
        return any(imp.iat_va == slot for imp in self.image.imports)

    def _describe_unstubbed(self, address: int, prev) -> str:
        target = "0x%08x" % address
        thunk = self.thunk_names.get(address)
        if thunk:
            target += " (import %s)" % thunk
        esp = self.read_reg("esp")
        try:
            top = self.read_u32(esp)
        except UcError:
            top = None
        site = "block 0x%08x..0x%08x" % (prev[0], prev[0] + prev[1])
        if top is not None and prev[0] and top == prev[0] + prev[1]:
            site += ", call returns to 0x%08x" % top
        elif top is not None:
            site += ", stack top 0x%08x" % top
        return "unstubbed target %s reached from %s" % (target, site)

    def _x87_variant(self, x87_args, ret) -> int:
        key = (tuple(s.size for s in x87_args), ret.size if ret is not None and ret.is_float else 0)
        if key in self._variants:
            return self._variants[key]
        code = bytearray(b"\xDB\xE3")
        code += b"\xD9\x2D" + struct.pack("<I", CW_SLOT)
        for i, scalar in enumerate(x87_args):
            op = b"\xD9\x05" if scalar.size == 4 else b"\xDD\x05"
            code += op + struct.pack("<I", X87_SLOTS + 8 * i)
        after_index = len(code)
        code += b"\x68" + bytes(4)
        code += b"\xFF\x25" + struct.pack("<I", TARGET_SLOT)
        index = len(self._variants)
        start = SCRATCH + 0x800 + index * 0x80
        after = start + len(code)
        code[after_index + 1:after_index + 5] = struct.pack("<I", after)
        if ret is not None and ret.is_float:
            code += b"\xDB\x3D" + struct.pack("<I", RET80)
        code += b"\xD9\x35" + struct.pack("<I", ENV_AREA)
        end = start + len(code)
        code += b"\x90"
        if len(code) > 0x80:
            raise HarnessError("too many x87 arguments")
        self.uc.mem_write(start, bytes(code))
        self._variants[key] = start
        self._variant_end[start] = end
        return start

    def call(self, func: Original, values: Sequence, cw: int = DEFAULT_CW,
             fresh: bool = True) -> CallOutput:
        """Run func. With fresh=False the heap and the data sections keep what the caller built."""
        if len(values) != len(func.args):
            raise ValueError("expected %d argument values, got %d" % (len(func.args), len(values)))
        self.allowed = [(func.addr, func.size)] + [tuple(a) for a in func.allow]
        self._error = None
        self._prev_block = (0, 0)
        if fresh:
            self.reset_state()
        uc = self.uc

        x87_args, x87_values = [], []
        regs = {"ecx": 0, "edx": 0}
        stack = bytearray()
        buffers: Dict[int, Tuple[int, int]] = {}
        int_slots = {"thiscall": ["ecx"], "fastcall": ["ecx", "edx"]}.get(func.conv, [])
        for index, (atype, value) in enumerate(zip(func.args, values)):
            if isinstance(atype, T.St0):
                x87_args.append(atype.scalar)
                x87_values.append(T.encode(atype.scalar, value))
                continue
            if isinstance(atype, T.Buf):
                data = bytes(value)
                size = atype.size if atype.size is not None else len(data)
                data = data.ljust(size, b"\0")
                addr = self.heap.alloc(atype.offset + size + 16) + atype.offset
                uc.mem_write(addr, data)
                buffers[index] = (addr, size)
                word = struct.pack("<I", addr)
            else:
                word = T.encode(atype, value)
                if atype.size < 4:
                    ext = word + (b"\xFF" * (4 - atype.size) if atype.signed and word[-1] & 0x80
                                  else bytes(4 - atype.size))
                    word = ext
            small = len(word) == 4 and not (isinstance(atype, T.Scalar) and atype.is_float)
            if small and int_slots:
                regs[int_slots.pop(0)] = struct.unpack("<I", word)[0]
            else:
                stack += word
        self.heap.alloc(16)

        sp = STACK_BASE + STACK_SIZE - 0x1000 - len(stack) - (len(stack) % 16)
        uc.mem_write(sp, bytes(stack))
        stack_bytes = len(stack)
        expected_esp = sp + (0 if func.conv == "cdecl" else stack_bytes)

        uc.mem_write(CW_SLOT, struct.pack("<H", cw))
        uc.mem_write(TARGET_SLOT, struct.pack("<I", func.addr))
        for i, raw in enumerate(x87_values):
            uc.mem_write(X87_SLOTS + 8 * i, raw)
        start = self._x87_variant(x87_args, func.ret if isinstance(func.ret, T.Scalar) else None)
        end = self._variant_end[start]

        for name in REGS:
            uc.reg_write(REGS[name], 0)
        for name, magic in MAGIC_SAVED.items():
            uc.reg_write(REGS[name], magic)
        uc.reg_write(REGS["ecx"], regs["ecx"])
        uc.reg_write(REGS["edx"], regs["edx"])
        uc.reg_write(REGS["esp"], sp)
        uc.reg_write(REGS["eflags"], 0x202)

        try:
            uc.emu_start(start, end, count=self.max_insns)
        except UcError as exc:
            if self._error is None:
                self._error = self._translate(exc)
        if self._error is not None:
            raise self._error
        if self.read_reg("eip") != end:
            raise RunawayExecution("instruction limit %d reached at eip 0x%08x"
                                   % (self.max_insns, self.read_reg("eip")))
        self._check_postconditions(func, expected_esp)
        found = self.heap.check_guards(self._block_class)
        if found and fresh:
            raise HarnessError("; ".join(found))
        st0 = None
        if isinstance(func.ret, T.Scalar) and func.ret.is_float:
            st0 = self.read(RET80, 10)
        tags = struct.unpack("<I", self.read(ENV_AREA + 8, 4))[0] & 0xFFFF
        return CallOutput(self.read_reg("eax"), self.read_reg("edx"), st0,
                          {i: self.read(a, n) for i, (a, n) in buffers.items()}, tags, cw)

    def _block_class(self, addr: int, size: int) -> Optional[str]:
        """The class of an object block, from the vtable pointer in its first bytes."""
        if size < 4:
            return None
        return self.vtables.get(self.read_u32(addr))

    def _check_postconditions(self, func: Original, expected_esp: int):
        esp = self.read_reg("esp")
        if esp != expected_esp:
            raise HarnessError("stack pointer after return is 0x%x, expected 0x%x: "
                               "wrong calling convention or argument list?" % (esp, expected_esp))
        for name, magic in MAGIC_SAVED.items():
            if self.read_reg(name) != magic:
                raise HarnessError("callee-saved register %s was changed (0x%08x)"
                                   % (name, self.read_reg(name)))

    def _translate(self, exc: UcError) -> Exception:
        eip = self.read_reg("eip")
        code = exc.errno
        names = {uc_const.UC_ERR_READ_UNMAPPED: "read from unmapped memory",
                 uc_const.UC_ERR_WRITE_UNMAPPED: "write to unmapped memory",
                 uc_const.UC_ERR_FETCH_UNMAPPED: "fetch from unmapped memory",
                 uc_const.UC_ERR_READ_PROT: "read from protected memory",
                 uc_const.UC_ERR_WRITE_PROT: "write to protected memory",
                 uc_const.UC_ERR_FETCH_PROT: "fetch from protected memory"}
        if code in names:
            return UnmappedAccess("%s at eip 0x%08x" % (names[code], eip))
        return HarnessError("emulator error %s at eip 0x%08x" % (exc, eip))
