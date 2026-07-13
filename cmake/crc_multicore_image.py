#!/usr/bin/env python3
"""
crc_multicore_image.py -- pure-Python replacement for TI's crc_multicore_image.exe.

Fills in the per-section and header CRC64 fields of a "multicore meta image"
(the combined MSS/BSS/DSS image produced by MulticoreImageGen.exe / this
toolchain's multicore_image_gen.py), so that the SBL's metaheader_parser.c /
image_validity.c can validate each section at boot time.

Reimplemented by disassembling crc_multicore_image.exe (a native 32-bit PE
binary, not .NET, so IL disassembly like out2rprc.py used doesn't apply
here -- objdump + Unicorn (to literally *execute* the disassembled x86
functions with controlled inputs and observe their outputs) were used
instead) and cross-checked byte-for-byte against the real tool's output,
including against a hand-crafted "pristine" (all-CRC-fields-zeroed) input
reconstructed from a real fixture. See calculate_crc64() below for how the
CRC algorithm itself was pinned down.

Two behaviors below are non-obvious and were confirmed empirically (running
the real .exe under wine, not just read off a spec), not guessed:

  1. The tool mutates its *input* file in place (per-section and header CRCs
     are written back into <input_file>), and *separately* writes a second
     copy of the whole file to <output_file> with every 4-byte word
     byte-order-reversed (LE -> BE). The tool's own --help text confirms
     this isn't a bug: it names the args "Multicore_image_name_LE" and
     "Multicore_image_name_BE". Concretely: `strings` on the exe shows
     `Syntax: ./<executable file name> <Multicore_image_name_LE>
     <Multicore_image_name_BE>`.

     This matters for this project's build: CMakeLists.txt invokes this
     tool as `crc_multicore_image.exe barebones_mss.bin barebones_mss.bin.tmp`
     and then runs gen_bincrc32.exe on `barebones_mss.bin` -- i.e. on the
     *input* path, not the byte-swapped `.tmp` output. The byte-swapped
     copy is a side effect this project's pipeline never consumes; the
     mutated input file (CRCs inserted, still little-endian) is what
     actually reaches the flashed image. Both behaviors are reproduced
     here for fidelity, since a future pipeline change could start
     consuming the BE copy.

  2. If the header's CRC fields are already non-zero, the tool refuses to
     do anything at all: no CRCs are (re)computed, the input file is left
     untouched, and -- contrary to what "just insert CRCs into a file that
     already has them" might suggest -- *no output file is written either*
     (confirmed: running the real tool against an already-CRC'd input
     produces no <output_file> on disk, just a "CRC is already present,
     exiting" message on stdout). This guards against accidentally
     re-running the tool on its own output and corrupting an
     already-valid image.

On-wire format (all fields little-endian, matching this toolchain's
little-endian ARM target), matching the SBL's own reader at
ti/utils/sbl/src/metaheader_parser.c:

  Pre-header (24 bytes), struct SBL_MetaHeader_t fields in file order:
    metaHeaderStart  I   magic, must be 0x5254534D ("RTSM" as bytes,
                         SBL_META_HDR_START) for the SBL to accept the image
    numFiles         I   number of per-file entries that follow (<= 5)
    devId            I   opaque device identifier, passed through unchanged
    hdrCRCHi         I   high 32 bits of the CRC64 over the *entire* header
                         (this field plus hdrCRCLo, temporarily zeroed while
                         computing) -- filled in by this tool
    hdrCRCLo         I   low 32 bits of that same CRC64
    imageSize        I   total size of this file in bytes

  Then `numFiles` per-file entries (32 bytes each), immediately following
  the pre-header, struct SBL_ImageDetails_t fields in file order:
    fileType         I   image/core id (echoed from MulticoreImageGen's
                         input, e.g. which CPU this section targets)
    magicWord        I   subsystem-tagged magic (e.g. 0x35510000 for MSS)
    fileOffset       I   byte offset of this file's *raw* content elsewhere
                         in this same combined image
    fileCRCHi        I   high 32 bits of the CRC64 over data[fileOffset :
                         fileOffset+fileSize] -- filled in by this tool
    fileCRCLo        I   low 32 bits of that same CRC64
    fileSize         I   exact byte length of this file's content (not
                         padded/rounded -- the CRC covers exactly this many
                         bytes)
    reserved         II  two reserved words, always 0

  Then a trailer (8 bytes):
    shMemAlloc       I   shared memory allocation size (passed through)
    metaHeaderEnd    I   magic, must be 0x444E454D ("MEND" as bytes,
                         SBL_META_HDR_END)

  headerTotalSize = 24 + numFiles*32 + 8, which conveniently always equals
  (numFiles+1)*32 -- the pre-header (24) and trailer (8) sum to exactly one
  32-byte "slot", the same size as one per-file entry. The real tool relies
  on this (it never handles a header whose size isn't a multiple of 8), and
  so does this port: fileSize values that aren't a multiple of 4 exercise a
  code path in the real .exe ("Padded RPRC Image for Core ID:%d") that was
  never observed to run against this project's actual build artifacts (all
  of which are 8-byte-aligned RPRC/bin sections already, thanks to
  out2rprc.py and MulticoreImageGen's own padding), so it was never
  reverse-engineered and is intentionally NOT implemented here -- same
  spirit as out2rprc.py only implementing ELF, not legacy COFF, input.

CRC64 algorithm (calculate_crc64() below):

  This is the *exact* algorithm implemented by crc_multicore_image.exe's
  own `_calculate_crc`/`_byteSwap`/`_calculate_file_crc` functions, pinned
  down by loading those three functions' raw machine code into a Unicorn
  CPU emulator and calling them directly with controlled register/stack
  arguments (rather than by hand-decoding ~230 lines of x86 shift/xor
  boilerplate, which a compiler had generated to fake 64-bit arithmetic on
  a 32-bit target). It is also, independently, exactly what TI's SBL-side
  hardware CRC peripheral computes for its "ISO 3309" CRC64 mode (see
  ti/drivers/crc/src/crc.c, CRC_Type_64BIT / CRC_BitSwap_MSB /
  CRC_ByteSwap_ENABLED) -- confirmed against that driver's own verified
  test vector (16-byte ASCII input "pankaj kapoor!!!" -> CRC64
  0x77c41a42f112ad04), which calculate_crc64() below reproduces exactly.
  It does NOT match any standard named CRC-64 variant (XZ, ECMA-182,
  GO-ISO, ...) despite superficially resembling one (same polynomial as
  CRC-64/GO-ISO) -- this is a distinct bit ordering.

  Parameters, spelled out in full since "CRC" alone is ambiguous:
    width   = 64 bits
    poly    = 0x000000000000001B (i.e. x^64 + x^4 + x^3 + x + 1)
    init    = 0x0000000000000000
    refin   = false -- input bits are consumed MSB-first *within* each
                       byte, and bytes are consumed in the data's natural
                       (file) order -- i.e. this is NOT the usual
                       "reflected" byte-at-a-time table-driven CRC most
                       libraries default to. (The real tool's C source
                       calls a same-named "_byteSwap" step before feeding
                       each 8-byte chunk to the core CRC routine, but that
                       step only exists to undo x86's little-endian 32-bit
                       register loading of the two words it just fread()'d
                       -- once undone, the bits stream through in the same
                       order they appear in the file. A Python
                       implementation working on a `bytes` object never
                       has that x86-register-loading problem in the first
                       place, so it's simply omitted here; see the
                       byte-order proof in this file's git history / PR
                       description if you need to re-derive it.)
    refout  = false -- the raw 64-bit shift-register value is the result,
                       not bit-reversed
    xorout  = 0x0000000000000000 -- no final XOR
  No lookup table is used (matching the reference hardware, which computes
  this bit-serially); a table-driven version would be a valid, faster,
  drop-in replacement but isn't necessary at these image sizes.
"""
import argparse
import struct
import sys

META_HDR_START = 0x5254534D  # SBL_META_HDR_START, ASCII "RTSM" as an LE dword
META_HDR_END = 0x444E454D    # SBL_META_HDR_END,   ASCII "MEND" as an LE dword

CRC64_POLY = 0x000000000000001B
CRC64_MASK = (1 << 64) - 1


def calculate_crc64(data):
    """Bit-serial CRC64 exactly matching crc_multicore_image.exe's software
    implementation (and TI's SBL-side CRC64 hardware peripheral -- see the
    module docstring for how both were confirmed). init=0, poly=0x1B,
    MSB-first, no reflection, no final XOR."""
    crc = 0
    for byte in data:
        for bitpos in range(7, -1, -1):
            top = (crc >> 63) & 1
            bit = (byte >> bitpos) & 1
            crc = (crc << 1) & CRC64_MASK
            if top ^ bit:
                crc ^= CRC64_POLY
    return crc


def bswap32_buffer(data):
    """Reverse the byte order of every 4-byte word in `data`. This is
    literally the entirety of crc_multicore_image.exe's "LE -> BE"
    conversion pass (confirmed by diffing a real .exe run's output against
    the input word-by-word: all 118416 words of a real 473664-byte fixture
    reversed with zero exceptions, including inside opaque byte strings
    like the RPRC magic -- i.e. it is an unconditional word bswap over the
    whole file, not a format-aware endianness conversion of specific
    integer fields)."""
    if len(data) % 4 != 0:
        raise ValueError(
            "input size %d is not a multiple of 4 -- the real tool's "
            "byte-swap pass has never been observed operating on a "
            "non-word-aligned file" % len(data)
        )
    out = bytearray(len(data))
    for i in range(0, len(data), 4):
        out[i:i + 4] = data[i:i + 4][::-1]
    return bytes(out)


def insert_crcs(data):
    """Compute and write back the per-file and header CRC64 fields of a
    multicore meta image. Returns the (same-length) updated buffer, or
    None if the header already has a non-zero CRC (matching the real
    tool's refusal to touch an already-CRC'd image -- see module
    docstring point 2)."""
    data = bytearray(data)

    (meta_hdr_start, num_files, dev_id, hdr_crc_hi, hdr_crc_lo,
     image_size) = struct.unpack_from("<IIIIII", data, 0)

    if meta_hdr_start != META_HDR_START:
        raise ValueError(
            "bad meta header magic 0x%08X (expected 0x%08X)"
            % (meta_hdr_start, META_HDR_START))

    if hdr_crc_hi != 0 or hdr_crc_lo != 0:
        print("CRC is already present, exiting")
        return None

    header_total_size = (num_files + 1) * 32
    if header_total_size % 8 != 0:
        # Unreachable given num_files*32 is always a multiple of 8, but the
        # real tool has an explicit zero-padding path for this case
        # ("Padding header with zeroes...") that was never exercised
        # against real build artifacts and so isn't reimplemented here.
        raise NotImplementedError(
            "header size %d bytes is not 8-byte aligned; the real tool's "
            "header-padding path for this case was not reverse-engineered"
            % header_total_size)

    print("size of App Image is %d bytes" % len(data))

    # Per-file CRCs first: each covers exactly fileSize raw bytes starting
    # at fileOffset elsewhere in this same combined image -- NOT the
    # (possibly larger, padded) per-file entry region, just the file's own
    # content range.
    for i in range(num_files):
        entry_off = 24 + i * 32
        (file_type, magic_word, file_offset, _crc_hi, _crc_lo,
         file_size) = struct.unpack_from("<IIIIII", data, entry_off)

        if file_size % 4 != 0:
            # See the module docstring: the real tool's per-file padding
            # path ("Padded RPRC Image for Core ID:%d") was never observed
            # against real, already-8-byte-aligned build artifacts.
            raise NotImplementedError(
                "file %d size %d bytes is not 4-byte aligned; the real "
                "tool's file-padding path for this case was not "
                "reverse-engineered" % (i, file_size))

        print("cur_crc_read_addr %d" % file_offset)
        file_crc = calculate_crc64(data[file_offset:file_offset + file_size])
        struct.pack_into("<II", data, entry_off + 12,
                          file_crc >> 32, file_crc & 0xFFFFFFFF)

    # Header CRC last, computed over the whole pre-header + per-file-entry
    # block (which by now contains the just-written per-file CRCs) with
    # only this header's own hdrCRCHi/hdrCRCLo fields held at zero -- the
    # same "zero the CRC field you're about to fill, then CRC the whole
    # struct" trick the SBL's own C reader uses to validate it (see
    # metaheader_parser.c: SBL_metaHeaderParser() zeroing
    # metaHeaderLcl[3]/[4] before calling CRC_computeSignature()).
    header_crc = calculate_crc64(bytes(data[0:header_total_size]))
    struct.pack_into("<II", data, 12,
                      header_crc >> 32, header_crc & 0xFFFFFFFF)

    return bytes(data)


def convert(input_path, output_path):
    with open(input_path, "rb") as f:
        data = f.read()

    updated = insert_crcs(data)
    if updated is None:
        # Matches the real tool: on an already-CRC'd input, neither the
        # input nor an output file is touched.
        return

    # The real tool mutates its LE input file in place with the inserted
    # CRCs (via a temp-file-plus-rename dance, presumably for a crash-safe
    # atomic replace -- Python doesn't need that ceremony to reach the same
    # final on-disk state, since a single write() here can't leave the
    # file half-written the way a killed process mid-rename hypothetically
    # could).
    with open(input_path, "wb") as f:
        f.write(updated)

    # ...and separately writes a whole-file word-byte-swapped ("BE") copy
    # to the output path. See module docstring point 1 for why this
    # project's build doesn't actually consume this second file today.
    with open(output_path, "wb") as f:
        f.write(bswap32_buffer(updated))


def main():
    parser = argparse.ArgumentParser(
        description="Insert per-section and header CRC64s into a multicore "
                     "meta image (pure-Python replacement for "
                     "crc_multicore_image.exe).")
    parser.add_argument("input_file",
                         help="multicore meta image (LE); modified in place")
    parser.add_argument("output_file",
                         help="byte-swapped (BE) copy of the updated image")
    args = parser.parse_args()

    convert(args.input_file, args.output_file)


if __name__ == "__main__":
    main()
