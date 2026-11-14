"""Stubs for functions outside the tested code, and the scratch heap they use."""
from dataclasses import dataclass
from typing import Callable, Dict, List, Optional

HEAP_BASE = 0x20000000
HEAP_SIZE = 0x01000000
POOL_ALLOC = 0x718CB0
POOL_FREE = 0x718CF0


GUARD = 16
GUARD_FILL = 0xFD


@dataclass
class Block:
    size: int
    cls: Optional[str] = None
    freed: bool = False
    guard: int = GUARD
    reported: bool = False


class Heap:
    """Bump allocator over a mapped scratch region. Freed blocks are not reused.

    alloc gives untracked memory for the harness. new_block gives a tracked block;
    release marks it freed and records a fault for a double free or an unknown pointer.

    Every tracked block has guard bytes (value 0xFD) before and behind it. check_guards finds a block
    whose guard bytes changed, which means that code wrote outside the block.
    """

    def __init__(self):
        self.next = HEAP_BASE
        self.blocks: Dict[int, Block] = {}
        self.faults: List[str] = []
        self._write = None
        self._read = None

    def bind(self, write, read):
        """Give the heap the memory access that it needs to place and check the guard bytes."""
        self._write, self._read = write, read

    def reset(self):
        self.next = HEAP_BASE
        self.blocks.clear()
        self.faults.clear()

    def new_block(self, size: int, cls: Optional[str] = None, align: int = 16) -> int:
        guard = max(GUARD, align)
        addr = self.alloc(size + 2 * guard, align) + guard
        self.blocks[addr] = Block(size, cls, guard=guard)
        if self._write is not None:
            self._write(addr - guard, bytes([GUARD_FILL]) * guard)
            self._write(addr + size, bytes([GUARD_FILL]) * guard)
        return addr

    def check_guards(self, class_of=None) -> List[str]:
        """Record a fault for each block whose guard bytes changed. Returns the new faults.

        class_of(address, size) names the class of a block that has no owner class, or returns None.
        """
        if self._read is None:
            return []
        found = []
        for addr, block in sorted(self.blocks.items()):
            if block.reported:
                continue
            problem = self._guard_problem(addr, block)
            if problem is None:
                continue
            block.reported = True
            cls = block.cls or (class_of(addr, block.size) if class_of else None)
            found.append("heap block overrun: block of %d bytes at 0x%08x%s: %s"
                         % (block.size, addr, " (" + cls + ")" if cls else "", problem))
        self.faults += found
        return found

    def _guard_problem(self, addr: int, block: Block) -> Optional[str]:
        before = self._read(addr - block.guard, block.guard)
        after = self._read(addr + block.size, block.guard)
        for i, byte in enumerate(before):
            if byte != GUARD_FILL:
                return "the byte at offset %d (before the start) was changed" % (i - block.guard)
        for i, byte in enumerate(after):
            if byte != GUARD_FILL:
                return "the byte at offset %d (%d past the end) was changed" % (block.size + i, i + 1)
        return None

    def release(self, addr: int):
        if addr == 0:
            return
        block = self.blocks.get(addr)
        if block is None:
            self.faults.append("free of 0x%08x, which is not the start of an allocated block" % addr)
        elif block.freed:
            self.faults.append("double free of 0x%08x (%d bytes%s)"
                               % (addr, block.size, ", " + block.cls if block.cls else ""))
        else:
            block.freed = True

    def alloc(self, size: int, align: int = 16) -> int:
        addr = (self.next + align - 1) & ~(align - 1)
        end = addr + max(size, 1)
        if end > HEAP_BASE + HEAP_SIZE:
            raise MemoryError("scratch heap is full")
        self.next = end
        return addr


class StubContext:
    """What a stub sees: the arguments of the call and the return value it sets."""

    def __init__(self, machine, conv: str):
        self.machine = machine
        self.conv = conv
        self.eax = 0
        self.edx = None
        self.st0 = None

    @property
    def heap(self) -> Heap:
        return self.machine.heap

    def reg(self, name: str) -> int:
        return self.machine.read_reg(name)

    def stack_arg(self, index: int) -> int:
        """Return stack dword number index (0 is the first stack argument)."""
        esp = self.machine.read_reg("esp")
        return self.machine.read_u32(esp + 4 + 4 * index)

    def arg(self, index: int) -> int:
        """Return argument number index of the stub's calling convention."""
        if self.conv == "thiscall":
            return self.reg("ecx") if index == 0 else self.stack_arg(index - 1)
        if self.conv == "fastcall":
            if index < 2:
                return self.reg(("ecx", "edx")[index])
            return self.stack_arg(index - 2)
        return self.stack_arg(index)

    def read(self, addr: int, size: int) -> bytes:
        return self.machine.read(addr, size)

    def write(self, addr: int, data: bytes):
        self.machine.write(addr, data)

    def returns(self, eax: int = 0, edx: int = None):
        self.eax = eax & 0xFFFFFFFF
        self.edx = None if edx is None else edx & 0xFFFFFFFF


@dataclass
class Stub:
    """fn(ctx) runs in place of the function. nargs sets how many bytes the callee pops."""
    fn: Callable[[StubContext], None]
    nargs: int = 0
    conv: str = "cdecl"

    @property
    def cleanup(self) -> int:
        if self.conv == "cdecl":
            return 0
        if self.conv == "fastcall":
            return 4 * max(self.nargs - 2, 0)
        if self.conv == "thiscall":
            return 4 * max(self.nargs - 1, 0)
        return 4 * self.nargs


def _allocate(ctx: StubContext):
    ctx.returns(ctx.heap.new_block(ctx.arg(0)))


def _release(ctx: StubContext):
    ctx.heap.release(ctx.arg(0))
    ctx.returns(0)


def malloc_stub() -> Stub:
    """cdecl malloc(size) or operator new(size)."""
    return Stub(_allocate, nargs=1, conv="cdecl")


def free_stub() -> Stub:
    """cdecl free(ptr) or operator delete(ptr). The heap never reuses memory."""
    return Stub(_release, nargs=1, conv="cdecl")


def pool_stubs() -> Dict[int, Stub]:
    """Stubs for the exe pool allocator: 0x718CB0 allocates (cdecl, size), 0x718CF0 frees (cdecl, ptr)."""
    return {POOL_ALLOC: malloc_stub(), POOL_FREE: free_stub()}
