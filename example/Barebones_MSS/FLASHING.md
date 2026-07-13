# Running the Barebones MSS example on the AWR6843AOP

No debugger required. You flash the generated image into the on-board serial
(QSPI) flash over USB, then let the ROM bootloader run it.

This example is genuinely bare-metal: no SYS/BIOS, no RTOS at all -- its own
`src/startup_awr6843.asm` supplies the vector table and reset trampoline.
Two earlier attempts at this both hit a reliable data abort on the first MSS
peripheral register access after boot and were abandoned; this one fixes
that by explicitly initializing the Abort/Undefined-mode banked stack
pointers and resetting the VIM/clearing ESM before `main()` runs (see that
file's header comment, and the AWR6xxx_Toolchain memory notes, for the full
story). Confirmed booting on real hardware.

## What gets built

```
build/mss_program.xer4f   ELF (R4F/MSS) - not directly bootable
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

Requires (already installed on this machine):
- `ti-cgt-arm_16.9.6.LTS`, `mmwave_sdk_03_04_00_03` (paths at the top of
  `CMakeLists.txt` -- note the SDK path is deliberately `03_04_00_03`, not
  the `03_06_02_00-LTS` install also present on this machine)
- `python3` (runs `cmake/gen_flash_image.py`, the image-generation pipeline
  -- pure standard library, no pip packages, no mono/wine)

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
- **LED lights solid but never blinks, no console text:** `main()` reached the
  LED write (right before `uhal_uart_init()`) but is hung initializing the
  UART, or `soc_init()` itself failed (in which case the LED stays solid
  *off* instead -- see `src/main.c`). Check the UART pinmux/pad values in
  `src/main.c` against your board's schematic if you're on different
  hardware.
- **LED blinks but no console text:** `uhal_uart_init()` succeeded but
  `uhal_uart_transmit()` isn't reaching the terminal -- check you're on the
  Application/User UART COM port (not the logging one) at 115200 8N1.
- **Nothing at all, not even the LED:** the image likely isn't booting/running
  at all. Double check the SOP mode and that the flash actually completed
  ("Program Load complete" in UniFlash, not an error).
