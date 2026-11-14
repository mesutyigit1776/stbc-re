"""Read the Appc module init code of stbc.exe and write the constant and global tables.

Writes target/appc_constants.tsv and target/appc_globals.tsv.
Run with: python3 -I re/extract_appc_init.py
"""
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import api_surface  # noqa: E402

INIT_START, INIT_END = 0x65A250, 0x65B13A
TABLE_FILL_START, TABLE_FILL_END = 0x64C580, 0x65A24F
FIRST_LINK = 0x65A2B8
ADD_VAR_LINK = 0x5BAD80
INSTALL_CONSTANTS = 0x5BB130
MODULE_VARIABLE = 0x98F03C
CONSTANT_ENTRY_SIZE = 0x20
CONSTANT_KINDS = {1: "int", 2: "float", 3: "string", 4: "pointer"}
CONSTANT_EVIDENCE = "E0007"
GLOBAL_EVIDENCE = "E0008"


def write(path, header, rows):
    with open(path, "w") as f:
        f.write("\t".join(header) + "\n")
        for row in rows:
            f.write("\t".join(row) + "\n")


def decode_calls(exe):
    """Walk the init code after the first link call; return (call target, pushed values) pairs."""
    va, pushes, calls = FIRST_LINK, [], []
    code = exe.data
    while va < INIT_END:
        o = exe.offset(va)
        op = code[o]
        if op == 0x68:
            pushes.append(struct.unpack_from("<I", code, o + 1)[0])
            va += 5
        elif op in (0x50, 0x51, 0x52, 0x57):
            pushes.append(("reg", op))
            va += 1
        elif op == 0xA1:
            va += 5
        elif op == 0x8B and code[o + 1] in (0x0D, 0x15):
            assert struct.unpack_from("<I", code, o + 2)[0] == MODULE_VARIABLE
            va += 6
        elif op == 0x83 and code[o + 1] == 0xC4:
            va += 3
        elif op == 0xE8:
            rel = struct.unpack_from("<i", code, o + 1)[0]
            calls.append(((va + 5 + rel) & 0xFFFFFFFF, pushes, va))
            pushes = []
            va += 5
        elif op == 0x5F or op == 0xC3:
            va += 1
        else:
            sys.exit("unexpected opcode %02x at 0x%x" % (op, va))
    return calls


def variable_of(exe, getter):
    """Return (variable address, getter shape) from the first bytes of a getter."""
    o = exe.offset(getter)
    code = exe.data[o:o + 16]
    u32 = lambda i: struct.unpack_from("<I", code, i)[0]
    if code[0] == 0xD9 and code[1] == 0x05:
        return u32(2), "float"
    if code[0] == 0xA1 and code[5] == 0x50 and code[6] == 0xE8:
        return u32(1), "int"
    if code[0] == 0xA1 and code[5] == 0x50 and code[6] == 0x68:
        return u32(7), "object"
    if code[0] == 0xA1 and code[5:7] == b"\x8b\x0d":
        return u32(7), "pointer"
    sys.exit("unknown getter shape at 0x%x" % getter)


REGISTERS = ["eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"]


def table_fill_writes(exe):
    """Walk the straight-line code that fills the constant table before the module init runs.

    Returns {data address: (value, address of the instruction that supplies it, source kind,
    memory address read or None)}.
    """
    code = exe.data
    regs, writes = {}, {}
    va = TABLE_FILL_START
    u32 = lambda at: struct.unpack_from("<I", code, exe.offset(at))[0]
    while va <= TABLE_FILL_END:
        b = code[exe.offset(va):exe.offset(va) + 2]
        op, modrm = b[0], b[1]
        reg, rm = REGISTERS[(modrm >> 3) & 7], REGISTERS[modrm & 7]
        absolute = (modrm & 0xC7) == 0x05
        if op == 0xA1:
            regs["eax"] = (u32(u32(va + 1)), va, "memory_read", u32(va + 1))
            size = 5
        elif op == 0xA3:
            writes[u32(va + 1)] = regs["eax"]
            size = 5
        elif op == 0xC7 and modrm == 0x05:
            writes[u32(va + 2)] = (u32(va + 6), va, "store_immediate", None)
            size = 10
        elif op == 0x89 and absolute:
            writes[u32(va + 2)] = regs[reg]
            size = 6
        elif op == 0x8B and absolute:
            regs[reg] = (u32(u32(va + 2)), va, "memory_read", u32(va + 2))
            size = 6
        elif op in (0x89, 0x8B) and modrm >= 0xC0:
            src, dst = (reg, rm) if op == 0x89 else (rm, reg)
            regs[dst] = regs[src]
            size = 2
        elif op in (0x31, 0x33) and modrm >= 0xC0 and reg == rm:
            regs[reg] = (0, va, "register_immediate", None)
            size = 2
        elif 0xB8 <= op <= 0xBF:
            regs[REGISTERS[op - 0xB8]] = (u32(va + 1), va, "register_immediate", None)
            size = 5
        elif 0x50 <= op <= 0x5F or op in (0x90, 0xC3):
            size = 1
        elif op == 0xF3 and modrm == 0xAB:
            size = 2
        else:
            sys.exit("unexpected opcode %02x at 0x%x" % (op, va))
        va += size
    return writes


def constants(exe, table, writes):
    """Read the constant table as the table fill code leaves it."""
    def field(address, size=4):
        parts = []
        for at in range(address, address + size, 4):
            if at in writes:
                parts.append(writes[at])
            else:
                parts.append((u32_file(at), at, "file_data", None))
        return parts

    def u32_file(at):
        return exe.u32(at)[0]

    rows = []
    va = table
    while True:
        kind, name_ptr, lvalue_field = field(va)[0][0], field(va + 4)[0][0], field(va + 8)[0]
        lvalue = lvalue_field[0]
        if kind == 0:
            return rows
        low, high = field(va + 0x10, 8)
        pvalue = field(va + 0x18)[0]
        name = exe.cstring(name_ptr)
        if kind == 1:
            value = str(struct.unpack("<i", struct.pack("<I", lvalue))[0])
            source = lvalue_field[1:]
        elif kind == 2:
            value = repr(struct.unpack("<d", struct.pack("<II", low[0], high[0]))[0])
            source = low[1:]
        elif kind == 3:
            value, source = exe.cstring(pvalue[0]), pvalue[1:]
        else:
            value, source = "pointer 0x%x" % pvalue[0], pvalue[1:]
        rows.append((name, value, CONSTANT_KINDS[kind], "0x%x" % source[0], source[1],
                     "0x%x" % source[2] if source[2] else ""))
        va += CONSTANT_ENTRY_SIZE


def main():
    exe = api_surface.Pe(api_surface.EXE)
    calls = decode_calls(exe)
    links = [(c, p) for c, p, _ in calls if c == ADD_VAR_LINK]
    install = [(p, at) for c, p, at in calls if c == INSTALL_CONSTANTS]
    assert len(install) == 1
    table = install[0][0][0]

    out_dir = os.path.join(ROOT, "target")
    globals_rows = []
    for _, pushes in links:
        setter, getter, name_ptr = pushes[-4], pushes[-3], pushes[-2]
        variable, shape = variable_of(exe, getter)
        globals_rows.append((exe.cstring(name_ptr), "0x%x" % getter, "0x%x" % setter,
                             "0x%x" % variable, shape))
    const_rows = constants(exe, table, table_fill_writes(exe))
    write(os.path.join(out_dir, "appc_constants.tsv"),
          ["name", "value", "value_type", "source_address", "source_kind", "memory_address", "evidence_id"],
          [row + (CONSTANT_EVIDENCE,) for row in const_rows])
    write(os.path.join(out_dir, "appc_globals.tsv"),
          ["name", "getter_address", "setter_address", "variable_address", "getter_shape",
           "evidence_id"],
          [row + (GLOBAL_EVIDENCE,) for row in globals_rows])
    print("constants: %d, globals: %d, constant table 0x%x"
          % (len(const_rows), len(globals_rows), table))


if __name__ == "__main__":
    main()
