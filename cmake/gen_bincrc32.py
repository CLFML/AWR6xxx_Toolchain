#!/usr/bin/env python3
"""
gen_bincrc32.py -- pure-Python replacement for TI's gen_bincrc32.exe.

This is the LAST stage of TI's 3-tool boot image pipeline
(MulticoreImageGen.exe -> crc_multicore_image.exe -> gen_bincrc32.exe). It
appends a single trailing whole-file CRC32 to a finished multicore boot
image, modifying the file **in place** (it takes one path argument, not
separate input/output paths).

Reimplemented without a spec: gen_bincrc32.exe is a plain 32-bit PE (not
.NET), so it can't be IL-disassembled the way out2rprc.exe was. However,
the same package also ships a Linux ELF build of the identical tool
(scripts/ImageCreator/append_bin_crc/gen_bincrc32, no ".exe") that is NOT
stripped and links against Ross Williams' well-known public-domain
"crcmodel.c" reference implementation (symbols cm_ini/cm_nxt/cm_blk/cm_crc/
cm_tab/reflect -- see his "A Painless Guide to CRC Error Detection
Algorithms"). Disassembling that ELF's main() shows it filling in a cm_t
parameter struct with, in order: width=32, poly=0x04C11DB7, init=0xFFFFFFFF,
refin=1, refout=1, xorout=0xFFFFFFFF before calling cm_ini() -- which is
exactly the parameter set for the standard "CRC-32/ISO-HDLC" algorithm (the
same one used by zlib/gzip/PNG/Ethernet, and by Python's zlib.crc32()). The
disassembly further shows the tool reads the entire input file from offset
0 to EOF in 4096-byte chunks (i.e. the CRC covers the whole file as it
existed before this tool ran, no header/footer exclusion), then writes the
4 resulting CRC bytes starting with the least-significant byte -- i.e.
little-endian -- appended at the file's end.

This was cross-checked empirically, not just by reading the disassembly:
running `zlib.crc32()` over a real pre-CRC pipeline fixture
(step3_crc.bin, 473664 bytes) and appending the 4-byte little-endian
result reproduced the real gen_bincrc32.exe's output (step4_final.bin,
473668 bytes) byte-for-byte.

Why a whole-file trailing CRC32 (as opposed to, say, a CRC embedded in a
header field): this final CRC32 is NOT read by the mmWave SDK's onboard
SBL (see ti/utils/sbl/src/*.c) -- the SBL's own image validation uses a
completely different, per-section CRC scheme computed by the AWR/IWR's
hardware CRC peripheral, embedded earlier in the metaheader. This trailing
whole-file CRC32 instead exists for the *host-side* flashing/programming
tools (e.g. UniFlash) to sanity-check that a .bin file was transferred to
target flash without corruption -- hence it must cover literally every byte
of the file gen_bincrc32.exe was handed, appended after the fact rather
than baked into any header field computed earlier in the pipeline.
"""
import argparse
import struct
import sys
import zlib


def append_crc32(path):
    with open(path, "rb") as f:
        data = f.read()

    # CRC-32/ISO-HDLC (poly 0x04C11DB7, init 0xFFFFFFFF, refin/refout,
    # xorout 0xFFFFFFFF) over the *entire* file as it exists before this
    # tool runs -- confirmed both by disassembly of the reference
    # implementation and by matching a real tool run byte-for-byte.
    crc = zlib.crc32(data) & 0xFFFFFFFF

    # Appended in place, 4 bytes, little-endian (LSB first) -- confirmed by
    # disassembly of the fwrite() call site, which stores eax's bytes into
    # the output buffer starting with the least-significant byte.
    with open(path, "ab") as f:
        f.write(struct.pack("<I", crc))

    return crc, len(data) + 4


def main():
    parser = argparse.ArgumentParser(
        description="Append a trailing whole-file CRC32 to a binary image "
                     "in place (pure-Python replacement for gen_bincrc32.exe).")
    parser.add_argument("input_file", help="binary file to append the CRC32 to, modified in place")
    args = parser.parse_args()

    crc, total_bytes = append_crc32(args.input_file)

    # Mirror the real tool's console output exactly (both this project's
    # build logs and the tool's own strings show this format).
    print(f">>>> Binary CRC32 = {crc:08x} <<<<")
    print(f">>>> Total bytes in binary file {total_bytes} <<<<")


if __name__ == "__main__":
    main()
