# Running the TI_RTOS_MSS example on the AWR6843AOP

No debugger required. You flash the generated image into the on-board serial
(QSPI) flash over USB, then let the ROM bootloader run it.

**This is the known-good reference example -- leave it working.**
`example/Barebones_MSS` starts as a byte-for-byte copy of this project and is
where active work happens (progressively stripping RTOS pieces towards true
bare-metal, and eventually switching from the mmWave SDK's own `ti/drivers/*`
over to `Universal_hal`, which is vendored here alongside this project but
not yet wired into the build). If you break Barebones_MSS, this directory is
what you diff against or copy back from.

This example boots on a minimal SYS/BIOS (TI-RTOS), not bare-metal. That's a
deliberate choice, not an oversight: no hand-rolled bare-metal boot (custom
vector table + reset trampoline, no RTOS at all) was ever proven to boot on
the AWR6843AOP board this was developed against. SYS/BIOS's own startup does
essential low-level bring-up -- `SOC_init()` in `src/main.c` configures the
MPU, among other things -- that a from-scratch reset handler could not
practically replicate blind, without a JTAG debugger to observe where it was
failing.

## What gets built

```
build/mss_program.xer4f   ELF (R4F/MSS, SYS/BIOS-linked) - not directly bootable
build/ti_rtos_mss.bin   <-- FLASH THIS. Multicore metaimage: MSS app + RadarSS
                              firmware + a DSS placeholder, CRC-protected.
```

The raw `.xer4f` ELF **cannot** boot the radar. The ROM/SBL only boots the
multicore metaimage (`ti_rtos_mss.bin`), which is why the CMake build fuses
the MSS application with the RadarSS firmware (and a placeholder DSS
component -- see `CMakeLists.txt` comment on `DSS_PLACEHOLDER`) and appends
CRCs automatically.

## Build

```bash
cd example/TI_RTOS_MSS
cmake -G Ninja -S . -B build
cmake --build build --target xdc_gen   # first time only, see note below
cmake --build build
# -> build/ti_rtos_mss.bin
```

On a truly clean `build/` directory, the plain `cmake --build build` can fail
with `'configPkg/package/cfg/mss_rtos_per4f.oer4f' ... missing and no known
rule to make it` -- `xdc_gen`'s output isn't strictly ordered before the link
step in the shared toolchain's ninja graph. Building the `xdc_gen` target
explicitly once (as above) works around it; after that, plain `cmake --build
build` is fine for incremental builds.

The first `xdc_gen` run compiles a full SYS/BIOS library from source (via XDC
configuro) into `src/sysbios/`, which takes noticeably longer than a bare
compile; subsequent builds reuse it.

Requires (already installed on this machine):
- `ti-cgt-arm_16.9.6.LTS`, `mmwave_sdk_03_04_00_03`, `bios_6_73_01_01`,
  `xdctools_3_50_08_24_core` (paths at the top of `CMakeLists.txt` -- note the
  SDK path is deliberately `03_04_00_03`, not the `03_06_02_00-LTS` install
  also present on this machine, see the comment there)
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
     `example/TI_RTOS_MSS/build/ti_rtos_mss.bin`
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
  -f build/ti_rtos_mss.bin
```

(The GUI is the reliable path for mmWave; the CLI needs a serial target `.ccxml`.)

## Troubleshooting

- **Nothing blinks / no COM ports:** you are almost certainly in the wrong SOP
  mode. Flashing needs SOP 101; running needs SOP 001. Reset after every SOP
  change.
- **UniFlash "Can't connect":** wrong COM port, or the board is not in SOP 101.
- **LED lights solid but never blinks, no console text:** `main()` reached the
  LED write (right before `UART_init()`/`UART_open()` in `BlinkHelloTask`) but
  is hung opening the UART. Check the UART pinmux/pad values in `src/main.c`
  against your board's schematic if you're on different hardware.
- **LED blinks but no console text:** `UART_open()` succeeded but
  `UART_write()` isn't reaching the terminal -- check you're on the
  Application/User UART COM port (not the logging one) at 115200 8N1.
- **Nothing at all, not even the LED:** the image likely isn't booting/running
  at all. Double check the SOP mode and that the flash actually completed
  ("Program Load complete" in UniFlash, not an error).
