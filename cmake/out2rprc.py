#!/usr/bin/env python3
"""
out2rprc.py -- pure-Python replacement for TI's out2rprc.exe.

Converts a TI armcl-built ELF executable (a ".xer4f" file) into TI's "RPRC"
multicore boot image section format, without needing mono/wine to run TI's
.NET tool.

Reimplemented by disassembling out2rprc.exe's IL (via ikdasm) rather than
from any TI-published spec -- TI doesn't publish the RPRC format. The
behavior below, including the section padding rule in read_section(), is a
deliberate byte-for-byte port of what that tool actually does, not a
"cleaned up" reinterpretation; it was cross-checked by running both tools on
the same input .xer4f and diffing the output .rprc byte-for-byte.

Only ELF input is implemented (TI COFF is not) -- this toolchain's armcl
invocation only ever produces ELF (confirmed: "ELF 32-bit LSB executable,
ARM" via `file` on the built .xer4f), never legacy COFF.

RPRC file format (all fields little-endian, matching this toolchain's
little-endian ARM target):

  Header (24 bytes):
    magic        4s    b"RPRC"
    entry_point  Q     entry point address (informational only -- the SBL
                       reads and discards it, has zero effect on boot)
    section_count I    number of section entries that follow
    version      I     always 1
    reserved     I     always 0

  Then `section_count` section entries, sorted ascending by load_addr (NOT
  ELF section-table order -- e.g. a section at load_addr 0 comes before one
  at load_addr 0x100 even if the second appears earlier in the ELF's
  section header table), each:
    Section header (24 bytes):
      load_addr    Q   physical address to copy this section's bytes to;
                       derived from the PT_LOAD segment that contains the
                       section (see parse_elf()), not read directly off the
                       section header
      size         I   ELF section size (sh_size) rounded UP to a multiple
                       of 4 -- NOT the raw sh_size, and NOT the possibly-
                       larger padded byte count that follows; see
                       read_section_data()
      reserved1    I   always 0
      reserved2    Q   always 0
    Section data: `size` bytes of content, further zero-padded up to a
    multiple of 8 if `size` itself isn't (see read_section_data())
"""
import argparse
import struct
import sys

RPRC_MAGIC = b"RPRC"
RPRC_VERSION = 1

SHT_PROGBITS = 1
PT_LOAD = 1


class ElfSection:
    __slots__ = ("sh_type", "sh_addr", "sh_offset", "sh_size")

    def __init__(self, sh_type, sh_addr, sh_offset, sh_size):
        self.sh_type = sh_type
        self.sh_addr = sh_addr
        self.sh_offset = sh_offset
        self.sh_size = sh_size


class ElfSegment:
    __slots__ = ("p_type", "p_offset", "p_vaddr", "p_paddr", "p_filesz")

    def __init__(self, p_type, p_offset, p_vaddr, p_paddr, p_filesz):
        self.p_type = p_type
        self.p_offset = p_offset
        self.p_vaddr = p_vaddr
        self.p_paddr = p_paddr
        self.p_filesz = p_filesz


class LoadableSection:
    __slots__ = ("load_addr", "size", "file_offset")

    def __init__(self, load_addr, size, file_offset):
        self.load_addr = load_addr
        self.size = size
        self.file_offset = file_offset


def parse_elf(data):
    """Parse an Elf32 file, returning (entry_point, loadable_sections),
    where loadable_sections is a list of LoadableSection in ELF section
    header order (same order out2rprc.exe emits them in)."""
    if data[0:4] != b"\x7fELF":
        raise ValueError("not an ELF file")
    ei_class = data[4]
    if ei_class != 1:
        raise ValueError("only 32-bit ELF (ELFCLASS32) is supported")
    ei_data = data[5]
    if ei_data == 1:
        endian = "<"
    elif ei_data == 2:
        endian = ">"
    else:
        raise ValueError("invalid ELF data encoding")

    # Elf32_Ehdr (52 bytes): e_ident(16) already consumed above.
    (e_type, e_machine, e_version, e_entry, e_phoff, e_shoff, e_flags,
     e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum,
     e_shstrndx) = struct.unpack_from(endian + "HHIIIIIHHHHHH", data, 16)

    # Elf32_Phdr (32 bytes each): p_type, p_offset, p_vaddr, p_paddr,
    # p_filesz, p_memsz, p_flags, p_align.
    segments = []
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        (p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags,
         p_align) = struct.unpack_from(endian + "IIIIIIII", data, off)
        segments.append(ElfSegment(p_type, p_offset, p_vaddr, p_paddr, p_filesz))

    # Elf32_Shdr (40 bytes each): sh_name, sh_type, sh_flags, sh_addr,
    # sh_offset, sh_size, sh_link, sh_info, sh_addralign, sh_entsize.
    def read_shdr(index):
        off = e_shoff + index * e_shentsize
        (sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size, sh_link,
         sh_info, sh_addralign, sh_entsize) = struct.unpack_from(
            endian + "IIIIIIIIII", data, off)
        return ElfSection(sh_type, sh_addr, sh_offset, sh_size)

    shnum = e_shnum
    if shnum == 0 and e_shoff != 0:
        # Standard ELF "extended numbering": section[0].sh_size holds the
        # real count when there are too many sections for a 16-bit e_shnum.
        shnum = read_shdr(0).sh_size

    loadable = []
    for i in range(shnum):
        sec = read_shdr(i)
        if sec.sh_size == 0 or sec.sh_type != SHT_PROGBITS:
            continue

        load_addr = None
        for seg in segments:
            if seg.p_type != PT_LOAD or seg.p_filesz == 0:
                continue
            if not (seg.p_vaddr <= sec.sh_addr < seg.p_vaddr + seg.p_filesz):
                continue
            if not (seg.p_offset <= sec.sh_offset < seg.p_offset + seg.p_filesz):
                continue
            load_addr = seg.p_paddr + (sec.sh_addr - seg.p_vaddr)
            break

        if load_addr is None:
            # SHT_PROGBITS but not part of any loadable segment (e.g. a
            # debug section) -- not part of the boot image.
            continue

        # Section size is rounded up to a multiple of 4 -- this rounded
        # value is what ends up in the RPRC section header's "size" field,
        # not the raw sh_size.
        size = (sec.sh_size + 3) & ~3
        loadable.append(LoadableSection(load_addr, size, sec.sh_offset))

    # Output order is ascending by load address, not ELF section-table
    # order -- e.g. .vecs (loadAddr 0) comes before .text (loadAddr 0x100)
    # even though .text appears earlier in the section header table.
    loadable.sort(key=lambda s: s.load_addr)

    return e_entry, loadable


def read_section_data(data, section):
    """Read a section's raw bytes and zero-pad to a multiple of 8.
    section.size is already 4-byte aligned (see parse_elf()), so the only
    possible remainder mod 8 is 0 or 4 -- this pads the written content up
    to the next multiple of 8 without changing the "size" field already
    recorded in the RPRC section header (which stays at the 4-aligned,
    possibly-not-8-aligned value)."""
    start = section.file_offset
    end = start + section.size
    content = bytearray(data[start:end])
    if len(content) < section.size:
        content.extend(b"\x00" * (section.size - len(content)))
    remainder = len(content) % 8
    if remainder != 0:
        content.extend(b"\x00" * (8 - remainder))
    return bytes(content)


def convert(input_path, output_path):
    with open(input_path, "rb") as f:
        data = f.read()

    entry_point, loadable_sections = parse_elf(data)

    with open(output_path, "wb") as out:
        out.write(RPRC_MAGIC)
        out.write(struct.pack("<QIII", entry_point, len(loadable_sections),
                               RPRC_VERSION, 0))
        for section in loadable_sections:
            content = read_section_data(data, section)
            out.write(struct.pack("<QIIQ", section.load_addr, section.size, 0, 0))
            out.write(content)


def main():
    parser = argparse.ArgumentParser(
        description="Convert a TI armcl ELF executable to RPRC format "
                     "(pure-Python replacement for out2rprc.exe).")
    parser.add_argument("input_file", help="input ELF executable (.xer4f)")
    parser.add_argument("output_file", help="output RPRC file")
    args = parser.parse_args()

    convert(args.input_file, args.output_file)
    print("File conversion complete!")


if __name__ == "__main__":
    main()
