# Running the Barebones MSS example on the AWR6843AOP

No debugger required. You flash the generated image into the on-board serial
(QSPI) flash over USB, then let the ROM bootloader run it.

**`example/TI_RTOS_MSS` is the known-good SYS/BIOS reference (leave it
alone).** This project started as a byte-for-byte copy of it and has since
been progressively stripped of RTOS pieces towards true bare-metal -- see
`src/main.c`'s header comment for the piece-by-piece history.

This example is now bare-metal: no SYS/BIOS, no XDC/configuro.
`src/startup_awr6843.asm` provides its own vector table and reset trampoline
(disable the MPU, jump to `_c_int00`), and `src/main.c` talks to the
GPIO/UART/clock-config hardware directly through registers instead of the
mmWave SDK's `ti/drivers` layer -- that SDK's precompiled drivers are
hard-linked against SYS/BIOS's `Hwi_*` functions with no non-RTOS
alternative, so keeping them wasn't an option once `sysbios.aer4f` was
dropped from the link. See `src/main.c`'s header comment (piece 4/N) for the
full reasoning, including why the very first bare-metal attempt on this board
failed for unrelated reasons (a broken CMake link rule, since fixed) rather
than because bare-metal boot itself doesn't work here.

**Not yet confirmed on hardware** -- structurally sound (builds, links,
entry point/reset trampoline verified via disassembly) but pending a flash
test. Update this note once verified.

## What gets built

```
build/mss_program.xer4f   ELF (R4F/MSS, bare-metal) - not directly bootable
build/barebones_mss.bin   <-- FLASH THIS. Multicore metaimage: MSS app + RadarSS
                              firmware + a DSS placeholder, CRC-protected.
```

The raw `.xer4f` ELF **cannot** boot the radar. The ROM/SBL only boots the
multicore metaimage (`barebones_mss.bin`), which is why the CMake build fuses
the MSS application with the RadarSS firmware (and a placeholder DSS
component -- see `CMakeLists.txt` comment on `DSS_PLACEHOLDER`) and appends
CRCs automatically.

## Build

```bash
cd example/Barebones_MSS
cmake -G Ninja -S . -B build
cmake --build build
# -> build/barebones_mss.bin
```

No `xdc_gen` step anymore -- this is a plain C/ASM build now, no SYS/BIOS or
XDC configuro involved.

Requires (already installed on this machine):
- `ti-cgt-arm_16.9.6.LTS`, `mmwave_sdk_03_04_00_03` (paths at the top of
  `CMakeLists.txt` -- the mmWave SDK is only used here for the RadarSS
  firmware binary and image-generation tools, not for any linked driver
  code; the SDK path is deliberately `03_04_00_03`, not the
  `03_06_02_00-LTS` install also present on this machine, see the comment
  there)
- `mono`  (runs TI's `out2rprc` .NET tool)
- `wine`  (runs TI's native-Win32 `MulticoreImageGen` / CRC tools)

## Flash it (UniFlash, ~2 minutes)

1. **Put the EVM in flashing mode.** Set the SOP jumpers/switches to SOP 101
   (functional-mode encoding differs by board revision -- on the AWR6843AOPEVM
   this is set via the on-board DIP switch per the schematic's "SOP OPTIONS"
   block).
2. Connect the micro-USB cable and reset. Two COM ports enumerate from the
   on-board CP2105 (an "Application/User UART" and a separate logging UART).
   Note the Application/User UART port -- this is also the one flashing uses.
3. Launch UniFlash.
   - New target configuration -> AWR6843 / xWR6843AOP.
   - Connection: Serial, select the Application/User UART port.
   - In Program, set Meta Image 1 to:
     `example/Barebones_MSS/build/barebones_mss.bin`
   - Click Load Image. Wait for "Program Load complete".
4. **Switch to functional mode:** SOP 001, then reset.

The ROM loads the MSS app + RadarSS firmware from flash and runs it.

## Expected result

The **GPIO_2 user LED** (device pad K13/PADAZ) blinks roughly every 500ms,
and the console (115200 8N1, same COM port used for flashing) prints
`Hello World!` on the same cadence. Both together is full confirmation;
either alone is a useful diagnostic (see Troubleshooting below).

## Command-line flashing (alternative)

If you prefer scripting over the GUI, TI's Dfu/`uniflash` CLI works too:

```bash
~/ti/uniflash_8.8.1/dslite.sh \
  --config <AWR6843_serial_config.ccxml> \
  -f build/barebones_mss.bin
```

(The GUI is the reliable path for mmWave; the CLI needs a serial target `.ccxml`.)

## Troubleshooting

- **Nothing blinks / no COM ports:** you are almost certainly in the wrong SOP
  mode. Flashing needs SOP 101; running needs SOP 001. Reset after every SOP
  change.
- **UniFlash "Can't connect":** wrong COM port, or the board is not in SOP 101.
- **LED lights solid but never blinks, no console text:** `blinkHello()`
  reached the LED write (right before `uartInit()`) but is hung in
  `uartInit()`/`uartWritePolling()` -- likely stuck waiting on `SCIFLR`'s
  TXRDY bit. Check the UART pinmux/pad values in `src/main.c` against your
  board's schematic if you're on different hardware.
- **LED blinks but no console text:** UART pins are muxed and the SCI is
  running, but nothing reaches the terminal -- check you're on the
  Application/User UART COM port (not the logging one) at 115200 8N1.
- **Nothing at all, not even the LED:** the image likely isn't booting/running
  at all. Double check the SOP mode and that the flash actually completed
  ("Program Load complete" in UniFlash, not an error).
