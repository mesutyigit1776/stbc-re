"""Parse the PE headers of a 32-bit Windows executable."""
import struct
from dataclasses import dataclass, field
from typing import List

PAGE = 0x1000
SCN_EXEC = 0x20000000
SCN_READ = 0x40000000
SCN_WRITE = 0x80000000


@dataclass
class Section:
    name: str
    va: int
    virtual_size: int
    raw_offset: int
    raw_size: int
    flags: int

    @property
    def mapped_size(self) -> int:
        return (max(self.virtual_size, self.raw_size) + PAGE - 1) & ~(PAGE - 1)

    @property
    def writable(self) -> bool:
        return bool(self.flags & SCN_WRITE)

    @property
    def executable(self) -> bool:
        return bool(self.flags & SCN_EXEC)


@dataclass
class Import:
    dll: str
    name: str
    iat_va: int

    @property
    def label(self) -> str:
        return "%s!%s" % (self.dll, self.name)


@dataclass
class PEImage:
    data: bytes
    image_base: int
    size_of_image: int
    sections: List[Section] = field(default_factory=list)
    imports: List[Import] = field(default_factory=list)

    def section_at(self, va: int):
        for s in self.sections:
            start = self.image_base + s.va
            if start <= va < start + s.mapped_size:
                return s
        return None

    def section_bytes(self, s: Section) -> bytes:
        raw = self.data[s.raw_offset:s.raw_offset + min(s.raw_size, s.mapped_size)]
        return raw + bytes(s.mapped_size - len(raw))


def _cstr(data: bytes, off: int) -> str:
    end = data.index(b"\0", off)
    return data[off:end].decode("ascii", "replace")


def _rva_to_offset(sections: List[Section], rva: int) -> int:
    for s in sections:
        if s.va <= rva < s.va + max(s.virtual_size, s.raw_size):
            return s.raw_offset + rva - s.va
    raise ValueError("RVA 0x%x is in no section" % rva)


def parse_pe(data: bytes) -> PEImage:
    if data[:2] != b"MZ":
        raise ValueError("not a PE file")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("bad PE signature")
    nsec, = struct.unpack_from("<H", data, pe + 6)
    opt_size, = struct.unpack_from("<H", data, pe + 20)
    opt = pe + 24
    if struct.unpack_from("<H", data, opt)[0] != 0x10B:
        raise ValueError("not a PE32 image")
    image_base, = struct.unpack_from("<I", data, opt + 28)
    size_of_image, = struct.unpack_from("<I", data, opt + 56)
    import_rva, import_size = struct.unpack_from("<II", data, opt + 96 + 8)
    image = PEImage(data, image_base, size_of_image)
    table = opt + opt_size
    for i in range(nsec):
        o = table + 40 * i
        name = data[o:o + 8].rstrip(b"\0").decode("ascii", "replace")
        vsize, va, rsize, roff = struct.unpack_from("<IIII", data, o + 8)
        flags, = struct.unpack_from("<I", data, o + 36)
        image.sections.append(Section(name, va, vsize, roff, rsize, flags))
    if import_rva and import_size:
        image.imports = _parse_imports(data, image.sections, import_rva)
    return image


def _parse_imports(data, sections, import_rva) -> List[Import]:
    out = []
    off = _rva_to_offset(sections, import_rva)
    while True:
        oft, _, _, name_rva, first = struct.unpack_from("<IIIII", data, off)
        if not (oft or first or name_rva):
            break
        dll = _cstr(data, _rva_to_offset(sections, name_rva))
        lookup = oft or first
        index = 0
        while True:
            entry, = struct.unpack_from("<I", data, _rva_to_offset(sections, lookup) + 4 * index)
            if entry == 0:
                break
            if entry & 0x80000000:
                name = "#%d" % (entry & 0xFFFF)
            else:
                name = _cstr(data, _rva_to_offset(sections, entry) + 2)
            out.append(Import(dll, name, first + 4 * index))
            index += 1
        off += 20
    return out
