#!/usr/bin/env python3
"""flash_dss.py -- pushes a TI cl6x-linked DSS ELF (e.g.
example/TI_RTOS_DSS/build/dss_program.xe674) into a running
GCC_FreeRTOS_DSS_Probe_MSS's UART CLI, using its dssLoad/dssStart commands.

ELF32 PT_LOAD parsing is the same generic approach cmake/gen_flash_image.py
uses for MSS's armcl-linked ELF (architecture-independent at the ELF
level -- only ELFCLASS32 + endianness matter, not e_machine), reused here
rather than shelling out to a TI-specific tool: builds one flat image
spanning [lowest p_paddr, highest p_paddr+p_memsz) across every PT_LOAD
segment, zero-filling both inter-segment gaps and each segment's own
p_memsz-p_filesz (BSS) tail. This matches dssLoad's single-contiguous-range
contract (see cli_dss_probe.c) and leaves no stale garbage from whatever
program was previously loaded at these addresses.

Usage: flash_dss.py <path-to-dss.xe674> <serial-port> [baud]
       (baud defaults to 115200, matching cli_dss_probe.c's DSS_PROBE_UART)
"""
import struct
import sys
import time

import serial


def parse_elf_flat_image(data: bytes) -> tuple[int, bytes]:
    if data[0:4] != b"\x7fELF":
        raise ValueError("not an ELF file")
    if data[4] != 1:
        raise ValueError("only 32-bit ELF (ELFCLASS32) is supported")
    ei_data = data[5]
    if ei_data == 1:
        endian = "<"
    elif ei_data == 2:
        endian = ">"
    else:
        raise ValueError("invalid ELF data encoding")

    (e_type, e_machine, e_version, e_entry, e_phoff, e_shoff, e_flags,
     e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum,
     e_shstrndx) = struct.unpack_from(endian + "HHIIIIIHHHHHH", data, 16)

    segments = []
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        (p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags,
         p_align) = struct.unpack_from(endian + "IIIIIIII", data, off)
        if p_type == 1 and p_memsz > 0:  # PT_LOAD
            segments.append((p_paddr, p_offset, p_filesz, p_memsz))

    if not segments:
        raise ValueError("no PT_LOAD segments found")

    base = min(s[0] for s in segments)
    end = max(s[0] + s[3] for s in segments)
    image = bytearray(end - base)

    for p_paddr, p_offset, p_filesz, p_memsz in segments:
        start = p_paddr - base
        image[start:start + p_filesz] = data[p_offset:p_offset + p_filesz]
        # image[] is already zero-initialized for the p_memsz-p_filesz tail.

    print(f"Entry point: 0x{e_entry:08x}")
    print(f"Flat image: base=0x{base:08x} size=0x{len(image):x} ({len(image)} bytes)")
    for p_paddr, p_offset, p_filesz, p_memsz in sorted(segments):
        print(f"  segment load=0x{p_paddr:08x} filesz=0x{p_filesz:x} memsz=0x{p_memsz:x}")

    return base, bytes(image)


def read_until_prompt(ser: serial.Serial, timeout_s: float = 5.0) -> str:
    """Reads until the CLI's 'DssProbe:/>' prompt reappears, returns
    everything read (minus the prompt itself) for the caller to inspect."""
    deadline = time.time() + timeout_s
    buf = b""
    prompt = b"DssProbe:/>"
    while time.time() < deadline:
        chunk = ser.read(ser.in_waiting or 1)
        if chunk:
            buf += chunk
            if buf.endswith(prompt):
                return buf[:-len(prompt)].decode(errors="replace")
    raise TimeoutError(f"no prompt within {timeout_s}s; got: {buf!r}")


def send_command(ser: serial.Serial, cmd: str, timeout_s: float = 5.0) -> str:
    ser.reset_input_buffer()
    ser.write((cmd + "\r").encode())
    return read_until_prompt(ser, timeout_s)


def main() -> None:
    if len(sys.argv) not in (3, 4):
        print(f"usage: {sys.argv[0]} <dss.xe674> <serial-port> [baud]", file=sys.stderr)
        sys.exit(1)

    elf_path = sys.argv[1]
    port = sys.argv[2]
    baud = int(sys.argv[3]) if len(sys.argv) == 4 else 115200

    with open(elf_path, "rb") as f:
        base, image = parse_elf_flat_image(f.read())

    with serial.Serial(port, baud, timeout=0.2) as ser:
        print("Connecting...")
        # Wake the prompt in case the console just sat idle -- send a bare
        # \r and drain whatever comes back before issuing real commands.
        ser.write(b"\r")
        time.sleep(0.3)
        ser.reset_input_buffer()

        print(f"dssLoad 0x{base:x} {len(image)} ...")
        ser.write(f"dssLoad {base:x} {len(image)}\r".encode())
        # cli_dss_load() prints its "Send N raw bytes now..." banner before
        # it starts reading -- give it a moment, then stream the image.
        time.sleep(0.3)
        ser.reset_input_buffer()
        ser.write(image)
        resp = read_until_prompt(ser, timeout_s=60.0)
        print(resp.strip())
        if "Done" not in resp:
            print("dssLoad did not report Done -- stopping.", file=sys.stderr)
            sys.exit(1)

        print("dssStart ...")
        resp = send_command(ser, "dssStart")
        print(resp.strip())
        if "Done" not in resp:
            print("dssStart did not report Done -- stopping.", file=sys.stderr)
            sys.exit(1)

        # MMWDEMO_DSS_ALIVE_CHECK_ADDRESS_DSS (mmw_messages.h): DSS writes
        # 0xCAFEF00D here as literally the first instruction of
        # MmwDemo_dssInitTask under VITALSIGNS_TESTBENCH_MODE -- same
        # alive-check example/GCC_FreeRTOS_VitalSigns_MSS's mbox_task.c
        # uses, just read here via dssPeek instead of a dedicated helper.
        print("Waiting for DSS alive-check magic...")
        for attempt in range(20):
            resp = send_command(ser, "dssPeek 21080000")
            print(resp.strip())
            if "CAFEF00D" in resp.upper():
                print("DSS is alive: alive-check magic confirmed.")
                return
            time.sleep(0.5)
        print("DSS alive-check magic never appeared -- DSS may not have started.", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
