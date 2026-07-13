#!/usr/bin/env python3
"""
gen_flash_image.py -- pure-Python replacement for TI's whole 4-tool boot
image pipeline (out2rprc.exe -> MulticoreImageGen.exe ->
crc_multicore_image.exe -> gen_bincrc32.exe), consolidated into one script.

Converts one or more per-core boot images -- a TI armcl ELF executable
(".xer4f", auto-converted to RPRC below) for MSS/DSS cores, or an
already-built raw binary for BSS/RadarSS -- into a single flashable
multicore image for the AWR6843's Secondary Bootloader (SBL): RPRC-encode
each ELF input, wrap all core images in a meta-header, fill in its CRC64
fields, then append a trailing whole-file CRC32 for host-side flashing
tools. No TI format spec exists for any of these; each stage was
reverse-engineered separately (out2rprc.exe is .NET and was IL-disassembled
with ikdasm; the other three are native PE and were reverse-engineered via
objdump/Unicorn-emulated disassembly of their machine code, cross-checked
against the SBL's own C reader source in ti/utils/sbl/) and the combined
output verified byte-for-byte against a real hardware-confirmed image built
by the original 4-tool mono/wine pipeline.

This single-file version does the whole conversion in memory and writes
the final image once -- no intermediate .rprc/.tmp files on disk, unlike
the original standalone tools (each of which insisted on a real file to
read/write, being separate processes with no way to share memory). One
behavior of the original pipeline is deliberately NOT reproduced here
because it no longer applies: the real crc_multicore_image.exe mutates its
input file in place and separately emits an unused byte-swapped ("BE")
copy to a second path, an artifact of it being a standalone tool bolted
onto another standalone tool's output file; a single in-memory pipeline has
no "input file" to mutate and nothing downstream ever consumed that BE
copy, so it's simply not produced.

CLI (drops the original MulticoreImageGen.exe's leading <LE/BE> byte-order
argument -- this project's build only ever used LE, and BE was never
observed against the real tool, so LE is now just hardcoded):

  gen_flash_image.py <dev_id> <shmem_alloc> <output_file> \
      <core_id_1> <file_1> [<core_id_2> <file_2> ...]

  <dev_id>         Device ID, written into the meta-header's devId field.
                   Hex-parsed regardless of an "0x" prefix -- this is what
                   the real MulticoreImageGen.exe does (confirmed: CLI arg
                   "37" ends up as header byte 0x37, not 0x25).
  <shmem_alloc>    Shared-memory allocation word, written into the
                   meta-header's shMemAlloc field. Also hex-parsed.
  <output_file>    Path to write the final flashable image to.
  <core_id> <file> pairs
                   One pair per core image, in the order they should
                   appear in the meta-header/output. <core_id> becomes
                   that image's 32-bit "magicWord" verbatim (hex-parsed) --
                   the SBL only inspects its upper 16 bits to identify
                   which core the image belongs to:
                     0x3551____  MSS   0xb551____  BSS/RadarSS
                     0xd551____  DSS   0xCF91____  xwr14xx config blob
                   <file> is auto-detected: if it starts with the ELF
                   magic (as a TI armcl ".xer4f" build does), it's
                   RPRC-encoded first (see elf_to_rprc()); otherwise it's
                   used as raw bytes unchanged (e.g. RadarSS firmware, a
                   DSS placeholder -- these are never ELF).

======================================================================
Stage 1: ELF -> RPRC (out2rprc.exe)
======================================================================

Only ELF32 input is implemented (TI COFF is not) -- this toolchain's armcl
invocation only ever produces ELF (confirmed via `file` on a built .xer4f).

RPRC format (all fields little-endian):
  Header (24 bytes): magic b"RPRC" (4) + entry_point (Q, verbatim from the
  ELF's e_entry, informational only -- the SBL reads and discards it) +
  section_count (I) + version=1 (I) + reserved=0 (I).

  Then `section_count` section entries, sorted ASCENDING BY LOAD ADDRESS
  (not ELF section-table order -- e.g. a section at load_addr 0 comes
  before one at load_addr 0x100 even if the second appears earlier in the
  ELF's section header table; this was the one subtle part that took a
  wrong first attempt to catch). Each entry: 24-byte header (load_addr Q,
  size I, reserved1 I=0, reserved2 Q=0) then `size` bytes of content,
  further zero-padded up to a multiple of 8 for writing (the *recorded*
  size field itself stays at its 4-aligned, possibly-not-8-aligned value).

  A section becomes an RPRC entry iff: ELF section type is SHT_PROGBITS,
  nonzero sh_size, and its sh_addr/sh_offset both fall inside some PT_LOAD
  program header's virtual-address and file-offset ranges. Its load_addr
  is derived by mapping through that segment: p_paddr + (sh_addr -
  p_vaddr) -- NOT the section's own sh_addr. .bss-type (SHT_NOBITS)
  sections are always excluded (no file content to copy). The recorded
  size is sh_size rounded UP to a multiple of 4, not the raw value.

======================================================================
Stage 2: wrap core images in a meta-header (MulticoreImageGen.exe)
======================================================================

Reimplemented by reading the SBL's own C parser (the authoritative spec;
TI publishes no format doc for this):
  ti/utils/sbl/src/metaheader_parser.c, multicoreimage_parser.c
  ti/utils/sbl/include/metaheader_parser.h, image_parser.h
then cross-checking every byte offset/magic/alignment rule against the
real tool's actual output, byte-for-byte.

IMPORTANT: the real MulticoreImageGen.exe leaves the per-file and
per-header CRC64 fields as uninitialized memory garbage rather than
computing them -- confirmed by running the real tool twice and observing
zeros one run, non-zero garbage another. This is harmless because stage 3
always unconditionally overwrites those exact fields regardless of input
state (confirmed: feeding stage 3 a zero-CRC input and a garbage-CRC input
produces byte-identical output either way). This function deterministically
writes zero into those fields rather than replicating the garbage.

Meta-header on-wire format (all fields little-endian):
  Fixed prefix (24 bytes): startMagic b"MSTR" (SBL_META_HDR_START) +
  numFiles (I) + devId (I) + hdrCRCHi (I, 0 until stage 3) + hdrCRCLo (I,
  0 until stage 3) + imageSize (I, total output size including padding).

  Then `numFiles` per-core entries (32 bytes each): fileType (I, always
  literal 1 -- the SBL parses but never reads this back, it derives the
  real per-file type from magicWord instead) + magicWord (I, the <core_id>
  argument verbatim) + fileOffset (I, always a multiple of 64) + fileCRCHi
  (I, 0 until stage 3) + fileCRCLo (I, 0 until stage 3) + fileSize (I,
  exact content length, NOT including padding) + reserved1 (I=0) +
  reserved2 (I=0).

  Then a trailer (8 bytes): shMemAlloc (I, the <shmem_alloc> argument
  verbatim) + endMagic b"MEND" (SBL_META_HDR_END).

  Alignment rule (round-up-to-64 applies twice): (1) after the header
  (32 + 32*numFiles bytes) the output is zero-padded up to the next
  multiple of 64 before the first core image's content begins -- matches
  SBL_stateMetaHeaderParser() in multicoreimage_parser.c, which rounds the
  header size up to 64 before switching parser states. (2) each core
  image's raw content is likewise followed by zero padding up to the next
  multiple of 64 -- including the LAST file, which grows the final output
  size even though there's no next file to align for (confirmed
  empirically against the real tool with 1-, 2- and 3-file fixtures).

======================================================================
Stage 3: fill in the CRC64 fields (crc_multicore_image.exe)
======================================================================

Each per-file CRC64 covers exactly data[fileOffset : fileOffset+fileSize]
(the raw content range, not the padded region). The header CRC64 covers
the whole pre-header + per-file-entry block (24 + numFiles*32 bytes) with
only hdrCRCHi/hdrCRCLo held at zero during the calculation -- computed
AFTER the per-file CRCs are already written into that same range (the same
"zero the field you're about to fill, then CRC the whole struct" trick the
SBL's own C reader uses to validate it -- see metaheader_parser.c zeroing
metaHeaderLcl[3]/[4] before calling CRC_computeSignature()).

CRC64 algorithm, pinned down by loading the real tool's disassembled
`_calculate_crc` machine code into a Unicorn CPU emulator and calling it
directly with controlled inputs (confirmed independently against TI's
SBL-side hardware CRC64 test vector in ti/drivers/crc/test/common/
crc_test.c: 16-byte ASCII "pankaj kapoor!!!" -> 0x77c41a42f112ad04, which
calculate_crc64() below reproduces exactly). It does NOT match any
standard named CRC-64 variant (XZ, ECMA-182, GO-ISO, ...) despite sharing
GO-ISO's polynomial -- this is a distinct bit ordering:
  width=64  poly=0x000000000000001B (x^64+x^4+x^3+x+1)  init=0
  refin=False (MSB-first, natural byte order)  refout=False  xorout=0
The reference hardware computes this bit-serially; calculate_crc64() below
instead uses a precomputed 256-entry lookup table (the standard table-
driven reformulation of the same bit-serial algorithm -- mathematically
identical output, not an approximation) since a pure-Python bit-serial
loop over a few hundred KB is slow (~4s) compared to the table-driven
byte-at-a-time version (~0.1s), and this script has no other way to get a
fast implementation without adding a compiled dependency (numpy, cffi,
etc.) that would work against the whole point of "no dependencies but
Python itself." The table-driven form is checked against the same TI
hardware test vector as the bit-serial derivation above.

Only 4-byte-aligned per-file sizes and an 8-byte-aligned header are
handled -- the real tool has zero-padding code paths for unaligned sizes
("Padded RPRC Image for Core ID:%d") that were never exercised against
this project's real (already-aligned) build artifacts, so they were never
reverse-engineered and aren't implemented here; same spirit as stage 1
only handling ELF, not legacy COFF.

======================================================================
Stage 4: append a trailing whole-file CRC32 (gen_bincrc32.exe)
======================================================================

Standard CRC-32/ISO-HDLC (poly 0x04C11DB7, init 0xFFFFFFFF, refin/refout,
xorout 0xFFFFFFFF -- the same variant zlib/gzip/PNG/Ethernet and Python's
zlib.crc32() use), computed over the entire image produced by stage 3, with
the 4-byte result appended little-endian. Pinned down via an unstripped
Linux ELF build of the same tool shipped alongside the Windows .exe in the
SDK, which links Ross Williams' well-known public-domain crcmodel.c.

This trailing CRC32 is NOT read by the SBL at boot (its own image
validation uses the completely different CRC64 scheme from stage 3,
computed by hardware); it exists purely for host-side flashing tools
(UniFlash etc.) to sanity-check that the .bin reached target flash intact.
"""
from __future__ import annotations

import struct
import sys
import zlib

# ---------------------------------------------------------------------------
# Stage 1: ELF -> RPRC
# ---------------------------------------------------------------------------

RPRC_MAGIC: bytes = b"RPRC"
RPRC_VERSION: int = 1

ELF_SHT_PROGBITS: int = 1
ELF_PT_LOAD: int = 1

# (p_type, p_offset, p_vaddr, p_paddr, p_filesz)
ElfSegment = tuple[int, int, int, int, int]
# (sh_type, sh_addr, sh_offset, sh_size)
ElfSectionHeader = tuple[int, int, int, int]
# (load_addr, size, file_offset)
RprcSection = tuple[int, int, int]


def _is_elf(data: bytes) -> bool:
    return data[0:4] == b"\x7fELF"


def elf_to_rprc(data: bytes) -> bytes:
    """Convert a TI armcl ELF32 executable's bytes to RPRC-encoded bytes.
    See the module docstring's "Stage 1" section for the exact format."""
    if data[4] != 1:
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
    segments: list[ElfSegment] = []
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        (p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags,
         p_align) = struct.unpack_from(endian + "IIIIIIII", data, off)
        segments.append((p_type, p_offset, p_vaddr, p_paddr, p_filesz))

    # Elf32_Shdr (40 bytes each): sh_name, sh_type, sh_flags, sh_addr,
    # sh_offset, sh_size, sh_link, sh_info, sh_addralign, sh_entsize.
    def read_shdr(index: int) -> ElfSectionHeader:
        off = e_shoff + index * e_shentsize
        (sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size, sh_link,
         sh_info, sh_addralign, sh_entsize) = struct.unpack_from(
            endian + "IIIIIIIIII", data, off)
        return sh_type, sh_addr, sh_offset, sh_size

    shnum = e_shnum
    if shnum == 0 and e_shoff != 0:
        # Standard ELF "extended numbering": section[0].sh_size holds the
        # real count when there are too many sections for a 16-bit e_shnum.
        shnum = read_shdr(0)[3]

    sections: list[RprcSection] = []
    for i in range(shnum):
        sh_type, sh_addr, sh_offset, sh_size = read_shdr(i)
        if sh_size == 0 or sh_type != ELF_SHT_PROGBITS:
            continue

        load_addr: int | None = None
        for p_type, p_offset, p_vaddr, p_paddr, p_filesz in segments:
            if p_type != ELF_PT_LOAD or p_filesz == 0:
                continue
            if not (p_vaddr <= sh_addr < p_vaddr + p_filesz):
                continue
            if not (p_offset <= sh_offset < p_offset + p_filesz):
                continue
            load_addr = p_paddr + (sh_addr - p_vaddr)
            break

        if load_addr is None:
            # SHT_PROGBITS but not part of any loadable segment (e.g. a
            # debug section) -- not part of the boot image.
            continue

        size = (sh_size + 3) & ~3  # round up to a multiple of 4
        sections.append((load_addr, size, sh_offset))

    # Output order is ascending by load address, not ELF section-table
    # order -- see the module docstring's "Stage 1" section.
    sections.sort(key=lambda s: s[0])

    out = bytearray()
    out += RPRC_MAGIC
    out += struct.pack("<QIII", e_entry, len(sections), RPRC_VERSION, 0)
    for load_addr, size, file_offset in sections:
        content = bytearray(data[file_offset:file_offset + size])
        if len(content) < size:
            content.extend(b"\x00" * (size - len(content)))
        remainder = len(content) % 8
        if remainder != 0:
            content.extend(b"\x00" * (8 - remainder))
        out += struct.pack("<QIIQ", load_addr, size, 0, 0)
        out += content

    return bytes(out)


# ---------------------------------------------------------------------------
# Stage 2: wrap core images in a meta-header
# ---------------------------------------------------------------------------

META_HDR_START: bytes = b"MSTR"  # SBL_META_HDR_START (0x5254534D) as LE bytes
META_HDR_END: bytes = b"MEND"    # SBL_META_HDR_END   (0x444E454D) as LE bytes

META_HDR_FIXED_SIZE: int = 24  # startMagic, numFiles, devId, hdrCRCHi/Lo, imageSize
META_ENTRY_SIZE: int = 32      # fileType, magicWord, fileOffset, fileCRCHi/Lo, fileSize, 2 reserved
META_HDR_TAIL_SIZE: int = 8    # shMemAlloc, endMagic
META_ALIGN: int = 64

# (magic_word, content_bytes)
CoreFile = tuple[int, bytes]


def _align_up(n: int, align: int) -> int:
    remainder = n % align
    return n if remainder == 0 else n + (align - remainder)


def build_meta_image(dev_id: int, shmem_alloc: int, core_files: list[CoreFile]) -> bytes:
    """core_files is a list of (magic_word, content_bytes) pairs, in the
    order they should appear in the header/output. See the module
    docstring's "Stage 2" section for the exact format; CRC64 fields are
    left at zero here, filled in by insert_crcs()."""
    num_files = len(core_files)
    header_size = _align_up(
        META_HDR_FIXED_SIZE + META_ENTRY_SIZE * num_files + META_HDR_TAIL_SIZE,
        META_ALIGN)

    # First pass: lay out where each file's content starts, so fileOffset
    # (needed in the header, written before any file content) is known up
    # front. Every file's region -- including the last -- is padded up to
    # the next 64-byte boundary, so this doubles as the running total size.
    offsets: list[int] = []
    cursor = header_size
    for _magic_word, content in core_files:
        offsets.append(cursor)
        cursor = _align_up(cursor + len(content), META_ALIGN)
    image_size = cursor

    out = bytearray()
    out += META_HDR_START
    out += struct.pack("<IIIII", num_files, dev_id, 0, 0, image_size)  # hdrCRCHi/Lo=0

    for (magic_word, content), offset in zip(core_files, offsets):
        out += struct.pack("<IIIIIIII",
                            1,             # fileType -- always literal 1
                            magic_word,
                            offset,
                            0, 0,          # fileCRCHi/Lo -- filled in later
                            len(content),
                            0, 0)          # reserved1, reserved2

    out += struct.pack("<I", shmem_alloc)
    out += META_HDR_END

    assert len(out) == META_HDR_FIXED_SIZE + META_ENTRY_SIZE * num_files + META_HDR_TAIL_SIZE
    out += b"\x00" * (header_size - len(out))

    for offset, (_magic_word, content) in zip(offsets, core_files):
        assert len(out) == offset
        out += content
        out += b"\x00" * (_align_up(len(out), META_ALIGN) - len(out))

    assert len(out) == image_size
    return bytes(out)


# ---------------------------------------------------------------------------
# Stage 3: fill in the CRC64 fields
# ---------------------------------------------------------------------------

CRC64_POLY: int = 0x000000000000001B
CRC64_MASK: int = (1 << 64) - 1


def _build_crc64_table() -> list[int]:
    """Standard table-driven reformulation of an MSB-first, non-reflected
    CRC: table[i] is the bit-serial result of running the byte value `i`
    (placed in the top byte of the register) through the same shift/XOR
    steps calculate_crc64() below would perform one bit at a time."""
    table: list[int] = []
    for i in range(256):
        crc = i << 56
        for _ in range(8):
            crc = ((crc << 1) ^ CRC64_POLY) if (crc & (1 << 63)) else (crc << 1)
            crc &= CRC64_MASK
        table.append(crc)
    return table


_CRC64_TABLE: list[int] = _build_crc64_table()


def calculate_crc64(data: bytes) -> int:
    """Table-driven CRC64 exactly matching the real crc_multicore_image.exe
    and TI's SBL-side CRC64 hardware peripheral -- see the module
    docstring's "Stage 3" section. init=0, poly=0x1B, MSB-first, no
    reflection, no final XOR."""
    crc = 0
    table = _CRC64_TABLE
    for byte in data:
        crc = ((crc << 8) ^ table[((crc >> 56) ^ byte) & 0xFF]) & CRC64_MASK
    return crc


def insert_crcs(data: bytes) -> bytes:
    """Fill in a meta image's per-file and header CRC64 fields. Returns the
    updated bytes."""
    data = bytearray(data)

    (meta_hdr_start, num_files, dev_id, hdr_crc_hi, hdr_crc_lo,
     image_size) = struct.unpack_from("<IIIIII", data, 0)

    if meta_hdr_start != struct.unpack("<I", META_HDR_START)[0]:
        raise ValueError("bad meta header magic 0x%08X" % meta_hdr_start)

    header_total_size = (num_files + 1) * META_ENTRY_SIZE
    if header_total_size % 8 != 0:
        # Unreachable given num_files*32 is always a multiple of 8; see the
        # module docstring's "Stage 3" section.
        raise NotImplementedError(
            "header size %d bytes is not 8-byte aligned; not reverse-"
            "engineered, see module docstring" % header_total_size)

    # Per-file CRCs first: each covers exactly fileSize raw bytes starting
    # at fileOffset elsewhere in this same image -- not the (possibly
    # larger, padded) per-file region, just the file's own content range.
    for i in range(num_files):
        entry_off = META_HDR_FIXED_SIZE + i * META_ENTRY_SIZE
        (file_type, magic_word, file_offset, _crc_hi, _crc_lo,
         file_size) = struct.unpack_from("<IIIIII", data, entry_off)

        if file_size % 4 != 0:
            raise NotImplementedError(
                "file %d size %d bytes is not 4-byte aligned; not "
                "reverse-engineered, see module docstring" % (i, file_size))

        file_crc = calculate_crc64(bytes(data[file_offset:file_offset + file_size]))
        struct.pack_into("<II", data, entry_off + 12,
                          file_crc >> 32, file_crc & 0xFFFFFFFF)

    # Header CRC last, computed over the whole pre-header + per-file-entry
    # block (which by now contains the just-written per-file CRCs) with
    # only this header's own hdrCRCHi/hdrCRCLo fields held at zero.
    header_crc = calculate_crc64(bytes(data[0:header_total_size]))
    struct.pack_into("<II", data, 12,
                      header_crc >> 32, header_crc & 0xFFFFFFFF)

    return bytes(data)


# ---------------------------------------------------------------------------
# Stage 4: append a trailing whole-file CRC32
# ---------------------------------------------------------------------------

def append_crc32(data: bytes) -> tuple[bytes, int]:
    """Append a standard CRC-32/ISO-HDLC of `data` to itself, little-endian.
    See the module docstring's "Stage 4" section."""
    crc = zlib.crc32(data) & 0xFFFFFFFF
    return data + struct.pack("<I", crc), crc


# ---------------------------------------------------------------------------
# Pipeline + CLI
# ---------------------------------------------------------------------------

def parse_hex(arg: str) -> int:
    """Parse a CLI numeric argument as hex, with or without a "0x" prefix --
    this is what the real MulticoreImageGen.exe does for
    dev_id/shmem_alloc/core_id."""
    return int(arg, 16)


def generate_flash_image(dev_id: int, shmem_alloc: int,
                          core_files: list[tuple[int, str]]) -> bytes:
    """core_files is a list of (magic_word, file_path) pairs. Returns the
    final flashable image bytes."""
    resolved: list[CoreFile] = []
    for magic_word, path in core_files:
        with open(path, "rb") as f:
            content = f.read()
        if _is_elf(content):
            content = elf_to_rprc(content)
        resolved.append((magic_word, content))

    image = build_meta_image(dev_id, shmem_alloc, resolved)
    print("size of App Image is %d bytes" % len(image))
    image = insert_crcs(image)
    image, crc = append_crc32(image)

    # Mirror the original tools' console output (this project's build logs
    # already show this format from the mono/wine pipeline).
    print(">>>> Binary CRC32 = %08x <<<<" % crc)
    print(">>>> Total bytes in binary file %d <<<<" % len(image))
    return image


def main() -> None:
    argv = sys.argv[1:]
    if len(argv) < 5 or len(argv) % 2 != 1:
        sys.stderr.write(
            "Usage: gen_flash_image.py <dev_id> <shmem_alloc> <output_file> "
            "<core_id_1> <file_1> [<core_id_2> <file_2> ...]\n")
        sys.exit(1)

    dev_id = parse_hex(argv[0])
    shmem_alloc = parse_hex(argv[1])
    output_file = argv[2]

    pair_args = argv[3:]
    core_files: list[tuple[int, str]] = [
        (parse_hex(pair_args[i]), pair_args[i + 1])
        for i in range(0, len(pair_args), 2)]

    image = generate_flash_image(dev_id, shmem_alloc, core_files)
    with open(output_file, "wb") as f:
        f.write(image)
    print("Flashable image ready: %s" % output_file)


if __name__ == "__main__":
    main()
