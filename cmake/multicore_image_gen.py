#!/usr/bin/env python3
"""
multicore_image_gen.py -- pure-Python replacement for TI's MulticoreImageGen.exe.

Wraps one or more per-core boot images (RPRC files for MSS/DSS produced by
out2rprc.py, or raw binaries for BSS/RadarSS and the xwr14xx config blob)
into a single "multicore meta image" with a TI-defined meta-header. TI's
Secondary Bootloader (SBL) on the AWR6843 parses this meta-header to find
and load each core's image -- see the SBL's own reader source, which is the
authoritative spec for this format (TI doesn't publish a spec doc for it):
  ti/utils/sbl/src/metaheader_parser.c      (header layout, byte offsets)
  ti/utils/sbl/include/metaheader_parser.h  (magic words, struct sizes)
  ti/utils/sbl/include/image_parser.h       (per-core subsystem IDs)
  ti/utils/sbl/src/multicoreimage_parser.c  (the 64-byte alignment rule)

Reimplemented by (1) reading the SBL's C parser (linked above) to get the
field layout, then (2) cross-checking every byte offset, magic number and
alignment rule against the real MulticoreImageGen binary shipped in the
SDK, by diffing its actual output against a hand-decoded hex dump.
MulticoreImageGen.exe is a native x86 PE, not .NET (confirmed via `file`;
its imports show it's a gcj-compiled C/Java program, not IL), so it can't
be IL-disassembled the way out2rprc.exe was. The same ImageCreator
directory also ships a native Linux ELF build of the *same* tool (no .exe
suffix) which was used here to cross-check the wine .exe's behavior
without any wine-specific ambiguity.

IMPORTANT / non-obvious finding about the CRC fields: this format has
per-file and per-header 64-bit CRC fields (see below), but
MulticoreImageGen does NOT compute them -- it always writes zero into
every CRC field. This was verified by running the real tool (both the
wine .exe and the native ELF build) on the project's actual MSS/BSS/DSS
inputs and observing all-zero CRC bytes in the output; every other byte,
including all header layout, magic words, offsets and padding, was
byte-identical to this script's output. The CRCs are filled in by the
*next* pipeline stage, crc_multicore_image.exe, which modifies the meta
image in place -- confirmed by running that tool on this script's output
and diffing: only the 4 CRC fields (header + one per file) change, and
the result then matches what a naive reading of "the fixture called
step2_multicore.bin" would suggest MulticoreImageGen itself produces. It
doesn't; that fixture was actually captured *after* crc_multicore_image
ran. This script deliberately matches MulticoreImageGen's real behavior
(zero CRCs) rather than pre-computing the CRCs those fields eventually
hold -- filling them in is crc_multicore_image.exe's job, not this one's.

CLI (mirrors the real tool's argument order -- see generateMetaImage.sh's
comment "format: MulticoreImageGen.exe <LE/BE> <DEV_ID> <SHMEM_ALLOC>
<OUTPUT FILE> <COREID> <IMAGE1> <COREID> <IMAGE2> .."):

  multicore_image_gen.py <LE> <dev_id> <shmem_alloc> <output_file> \
      <core_id_1> <file_1> [<core_id_2> <file_2> ...]

  <LE>            Byte order of the meta-header; only "LE" is implemented
                   -- the only value this project's build ever passes (see
                   Barebones_MSS's build steps) and the only one that could
                   be verified against the real tool. Passing "BE" to the
                   real tool made it hang under this environment's wine
                   install, so its exact behavior was never confirmed and
                   isn't guessed at here.
  <dev_id>         Device ID, written verbatim into the header's devId
                   field. Parsed as HEX regardless of an "0x" prefix --
                   confirmed by observing the CLI arg "37" (no 0x prefix,
                   as generateMetaImage.sh passes it) end up as header byte
                   0x37 (55 decimal), not 0x25 (37 decimal). I.e. the real
                   tool always parses this with the equivalent of
                   strtoul(arg, NULL, 16).
  <shmem_alloc>    Shared-memory allocation word, written verbatim into the
                   header's shMemAlloc field. Also hex-parsed (conventionally
                   passed with an explicit "0x" prefix, e.g. 0x00000006, but
                   the parsing rule is the same either way).
  <output_file>    Path to write the combined meta image to.
  <core_id> <file> pairs
                   One pair per core image. <core_id> becomes that image's
                   32-bit "magicWord" verbatim (hex-parsed) -- it is NOT a
                   literal memory address despite looking like one; the SBL
                   only ever inspects its upper 16 bits (SBL_SUBSYSTEM_MASK
                   / SBL_SUBSYSTEM_SHIFT in image_parser.h) to identify
                   which core the image belongs to:
                     0x3551____  MSS   0xb551____  BSS/RadarSS
                     0xd551____  DSS   0xCF91____  xwr14xx config blob
                   <file> is copied byte-for-byte with no parsing of its
                   contents -- MulticoreImageGen doesn't care whether it's
                   an RPRC file (MSS/DSS) or a raw binary (BSS/RadarSS); it
                   is purely a concatenator + header wrapper.

On-wire format (all fields little-endian, matching the <LE> mode this
project uses -- "BE" is not implemented, see above):

  Meta-header, fixed prefix (24 bytes):
    startMagic   4s   b"MSTR" (SBL_META_HDR_START = 0x5254534D read as a
                       little-endian uint32 -- that's what the 4 ASCII
                       bytes happen to decode to, it isn't meant to spell
                       anything in particular)
    numFiles     I    number of core-image entries that follow
    devId        I    device ID (see <dev_id> above)
    hdrCRCHi     I    upper 32 bits of a 64-bit CRC over this whole header
                       (with hdrCRCHi/hdrCRCLo themselves treated as zero
                       during the calculation, per metaheader_parser.c) --
                       always 0 here, see the CRC note above
    hdrCRCLo     I    lower 32 bits of the same CRC -- always 0 here
    imageSize    I    total size of the output file in bytes, including
                       all alignment padding (see below)

  Then `numFiles` per-core entries (32 bytes each):
    fileType     I    always the literal value 1 in every image this tool
                       produces. The SBL parses this field but never
                       actually reads it back to decide anything --
                       image_parser.c derives the real per-file type it
                       acts on from magicWord instead (see
                       SBL_imageParser()). Kept here purely for byte-layout
                       compatibility with the SBL's parser, which
                       unconditionally consumes 4 bytes for it.
    magicWord    I    the <core_id> CLI argument, verbatim (see above)
    fileOffset   I    byte offset of this file's content from the start of
                       the output file; always a multiple of 64 (see the
                       alignment rule below)
    fileCRCHi    I    upper 32 bits of a 64-bit CRC over this file's raw
                       content -- always 0, see the CRC note above
    fileCRCLo    I    lower 32 bits of the same CRC -- always 0
    fileSize     I    exact byte length of the file's content, NOT
                       including the zero padding that follows it in the
                       output
    reserved1    I    always 0
    reserved2    I    always 0

  Then, after all entries, 8 more bytes:
    shMemAlloc   I    the <shmem_alloc> CLI argument, verbatim
    endMagic     4s   b"MEND" (SBL_META_HDR_END = 0x444E454D as a
                       little-endian uint32)

  Alignment rule (the same round-up-to-64 formula applies twice):
    1. After the header (startMagic..endMagic, i.e. 32 + 32*numFiles
       bytes) the output is zero-padded up to the next multiple of 64
       before the first core image's content begins. This matches the SBL
       side: SBL_stateMetaHeaderParser() in multicoreimage_parser.c rounds
       the header's byte count up to 64 before switching parser states, so
       the generator has to lay out data the same way for the offsets to
       line up.
    2. Each core image's raw content is likewise followed by zero padding
       up to the next multiple of 64 -- including the LAST file, which
       grows the final output size even though there's no next file to
       align for. Confirmed empirically: a single-MSS-file image (47960
       bytes of content starting at offset 64) is padded from byte 48024
       up to 48064 with 40 zero bytes and nothing after; the real tool's
       own stderr output ("Number of zeros 40") corroborates this. The SBL
       source doesn't document this (SBL_imageParser() just reads exactly
       `fileSize` bytes per file and never looks past that); it's simply
       what the real generator does, verified by producing extra fixtures
       with 1, 2 and 3 input files via the real tool and diffing.
    64 was chosen because it's the same block size the SBL's transport /
    QSPI readers move the image in elsewhere in the SDK; this tool doesn't
    explain why, but it's the constant used consistently on the read side.
"""
import struct
import sys

META_HDR_START = b"MSTR"   # SBL_META_HDR_START (0x5254534D) as little-endian bytes
META_HDR_END = b"MEND"     # SBL_META_HDR_END   (0x444E454D) as little-endian bytes

HDR_FIXED_SIZE = 24        # startMagic, numFiles, devId, hdrCRCHi, hdrCRCLo, imageSize
ENTRY_SIZE = 32             # fileType, magicWord, fileOffset, fileCRCHi/Lo, fileSize, 2 reserved
HDR_TAIL_SIZE = 8           # shMemAlloc, endMagic
ALIGN = 64


def align_up(n, align=ALIGN):
    remainder = n % align
    return n if remainder == 0 else n + (align - remainder)


def parse_hex(arg):
    """Parse a CLI numeric argument as hex, with or without a "0x" prefix --
    this is what the real tool does for devId/shmem_alloc/core_id (see the
    devId disambiguation in the module docstring)."""
    return int(arg, 16)


def build(dev_id, shmem_alloc, core_files):
    """core_files is a list of (magic_word, file_path) pairs, in the order
    they should appear in the header/output (i.e. CLI argument order)."""
    contents = [open(path, "rb").read() for _, path in core_files]

    num_files = len(core_files)
    header_size = align_up(HDR_FIXED_SIZE + ENTRY_SIZE * num_files + HDR_TAIL_SIZE)

    # First pass: lay out where each file's content starts, so fileOffset
    # (needed in the header, written before any file content) is known
    # up front. Every file's region -- including the last -- is padded up
    # to the next 64-byte boundary (see the alignment rule in the
    # docstring), so this doubles as the running total output size.
    offsets = []
    cursor = header_size
    for content in contents:
        offsets.append(cursor)
        cursor = align_up(cursor + len(content))
    image_size = cursor

    out = bytearray()
    out += META_HDR_START
    out += struct.pack("<III", num_files, dev_id, 0)  # hdrCRCHi always 0, see docstring
    out += struct.pack("<II", 0, image_size)           # hdrCRCLo always 0, see docstring

    for (magic_word, _path), offset, content in zip(core_files, offsets, contents):
        out += struct.pack("<IIIIIIII",
                            1,             # fileType -- always literal 1, see docstring
                            magic_word,
                            offset,
                            0, 0,          # fileCRCHi/Lo -- always 0, see docstring
                            len(content),
                            0, 0)          # reserved1, reserved2

    out += struct.pack("<I", shmem_alloc)
    out += META_HDR_END

    assert len(out) == HDR_FIXED_SIZE + ENTRY_SIZE * num_files + HDR_TAIL_SIZE
    out += b"\x00" * (header_size - len(out))

    for offset, content in zip(offsets, contents):
        assert len(out) == offset
        out += content
        out += b"\x00" * (align_up(len(out)) - len(out))

    assert len(out) == image_size
    return bytes(out)


def main():
    argv = sys.argv[1:]
    if len(argv) < 6 or len(argv) % 2 != 0:
        sys.stderr.write(
            "Usage: multicore_image_gen.py <LE> <dev_id> <shmem_alloc> "
            "<output_file> <core_id_1> <file_1> [<core_id_2> <file_2> ...]\n")
        sys.exit(1)

    byte_order, dev_id_arg, shmem_alloc_arg, output_file = argv[0:4]
    if byte_order.upper() != "LE":
        # See the module docstring: BE was never observed against the real
        # tool (it hung under wine in this environment), so it's refused
        # here rather than silently guessed at.
        raise NotImplementedError(
            f"byte order {byte_order!r} is not implemented -- only LE has "
            "been verified against the real MulticoreImageGen tool")

    dev_id = parse_hex(dev_id_arg)
    shmem_alloc = parse_hex(shmem_alloc_arg)

    pair_args = argv[4:]
    core_files = [(parse_hex(pair_args[i]), pair_args[i + 1])
                  for i in range(0, len(pair_args), 2)]

    image = build(dev_id, shmem_alloc, core_files)
    with open(output_file, "wb") as f:
        f.write(image)


if __name__ == "__main__":
    main()
