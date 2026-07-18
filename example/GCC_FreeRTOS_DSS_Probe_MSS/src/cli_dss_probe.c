/*
 * cli_dss_probe.c -- UART command console for using MSS as a debug
 * probe/flasher for DSS, no JTAG required. Line reader/tokenizer/dispatch
 * follows the same hand-rolled pattern as
 * GCC_FreeRTOS_VitalSigns_MSS/src/cli_vitalsigns.c (peek/poke/dssStatus in
 * particular are lifted from there almost verbatim), but this project has
 * no mailbox protocol of its own -- see this repo's memory notes
 * (ti_rtos_dss_replica.md's follow-up discussion) for the design context.
 *
 * Intended workflow:
 *   1. Flash this MSS image (its DSS slot is prebuilt/placeholder_dss.bin,
 *      same placeholder every other example here uses -- it doesn't matter
 *      what's in it, since DSS stays halted until `dssStart`, and normally
 *      you'll overwrite its memory with `dssLoad` before that anyway).
 *   2. Power up. DSS is halted (SBL loads it powered-on-but-halted, same
 *      state soc_unhalt_dss() has always assumed -- see Universal_hal's
 *      soc_iwr68xx.c) while this CLI comes up on MSS.
 *   3. `dssLoad <hexDssAddr> <decSizeBytes>`, then send the raw bytes (a
 *      flat memory image anchored at hexDssAddr, e.g. objcopy -O binary
 *      output of a build like example/TI_RTOS_DSS's dss_program.xe674,
 *      NOT the .xe674/ELF itself) over the same UART. Repeat for each
 *      contiguous region the image occupies.
 *   4. `dssStart` to release the halt and let DSS run the freshly-written
 *      code from its reset vector.
 *   5. `dssPeek`/`dssPoke`/`dssStatus` for ongoing visibility while it
 *      runs.
 *
 * NOT implemented here: updating a DSS that's already running. The TRM's
 * Power OFF sequence (SWRU520E s5.4.2.3) needs DSS-side cooperation (an
 * ISR that acks a clock-stop request and idles) that no DSS firmware in
 * this toolchain currently implements -- attempting it from MSS alone
 * would just hang waiting for an ack that never comes. `dssLoad`/
 * `dssStart` above only cover the case where DSS hasn't started executing
 * yet, which is the one actually confirmed against the TRM's documented
 * Power ON sequence (s5.4.2.2) and this toolchain's own working
 * soc_unhalt_dss().
 */
#include <hal_uart.h>
#include <hal_soc.h>
#include <AWR6843.h>
#include <stdbool.h>
#include <string.h>
#include "cli_dss_probe.h"
#include "dss_mem.h"

#define DSS_PROBE_UART UART_PERIPHERAL_MSS_SCIA

#define CLI_LINE_MAX  128
#define CLI_MAX_ARGS  8

/* dssLoad streams UART bytes directly into DSS memory (via its translated
 * MSS-visible address) in chunks this size, printing a progress dot per
 * chunk -- avoids one huge blocking uhal_uart_receive() call with no
 * feedback, and avoids needing a staging buffer in MSS RAM for the whole
 * image. */
#define DSS_LOAD_CHUNK_SIZE 4096U

static void cli_write_line(const char* msg) {
    uhal_uart_transmit(DSS_PROBE_UART, (const uint8_t*)msg, strlen(msg));
}

/* Same hand-rolled parsers as cli_vitalsigns.c (no stdlib atoi/strtoul --
 * this project links freestanding, see that file's header for why), minus
 * the float parser this project has no use for. */
static uint32_t cli_parse_uint(const char* s) {
    uint32_t result = 0;
    while ((*s >= '0') && (*s <= '9')) {
        result = (result * 10U) + (uint32_t)(*s - '0');
        s++;
    }
    return result;
}

static uint32_t cli_parse_hex(const char* s) {
    uint32_t result = 0;

    if ((s[0] == '0') && ((s[1] == 'x') || (s[1] == 'X'))) {
        s += 2;
    }
    while (*s != '\0') {
        uint32_t digit;
        if ((*s >= '0') && (*s <= '9')) {
            digit = (uint32_t)(*s - '0');
        } else if ((*s >= 'a') && (*s <= 'f')) {
            digit = (uint32_t)(*s - 'a') + 10U;
        } else if ((*s >= 'A') && (*s <= 'F')) {
            digit = (uint32_t)(*s - 'A') + 10U;
        } else {
            break;
        }
        result = (result << 4) | digit;
        s++;
    }
    return result;
}

static void cli_report_hex32(uint32_t value) {
    static const char hex_digits[] = "0123456789ABCDEF";
    uint8_t buf[10];
    int i;
    buf[0] = '0';
    buf[1] = 'x';
    for (i = 0; i < 8; i++) {
        buf[2 + i] = (uint8_t)hex_digits[(value >> (28 - (i * 4))) & 0xFU];
    }
    uhal_uart_transmit(DSS_PROBE_UART, buf, sizeof(buf));
}

static size_t cli_read_line(char* buf, size_t max_len) {
    size_t len = 0;
    uint8_t ch;

    for (;;) {
        (void)uhal_uart_receive(DSS_PROBE_UART, &ch, 1);

        if ((ch == '\r') || (ch == '\n')) {
            cli_write_line("\r\n");
            break;
        }
        if ((ch == '\b') || (ch == 0x7FU)) {
            if (len > 0) {
                len--;
                cli_write_line("\b \b");
            }
            continue;
        }
        if (len < (max_len - 1U)) {
            buf[len++] = (char)ch;
            (void)uhal_uart_transmit(DSS_PROBE_UART, &ch, 1);
        }
    }
    buf[len] = '\0';
    return len;
}

static int32_t cli_tokenize(char* line, char* argv[], int32_t max_argv) {
    int32_t argc = 0;
    char* p = line;

    while ((*p != '\0') && (argc < max_argv)) {
        while (*p == ' ') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        argv[argc++] = p;
        while ((*p != '\0') && (*p != ' ')) {
            p++;
        }
        if (*p == ' ') {
            *p = '\0';
            p++;
        }
    }
    return argc;
}

/* GEMPWRSMCFG3/4 dump -- see AWR6843_DSSREG.h. Same fields
 * cli_vitalsigns.c's dssStatus reads, minus the vitalsigns-specific
 * alive-check magic value (this project has no fixed protocol running on
 * DSS to check for). */
static void cli_dss_status(int32_t argc, char* argv[]) {
    (void)argc;
    (void)argv;

    cli_write_line("GEMPWRSMCFG3 = ");
    cli_report_hex32(DSSREG->GEMPWRSMCFG3);
    cli_write_line(" (power domain ");
    cli_write_line(((DSSREG->GEMPWRSMCFG3 & DSSREG_GEMPWRSMCFG3_PWRSMMODESTATUS_MASK)
                     == (DSSREG_PWRSMMODESTATUS_ON << DSSREG_GEMPWRSMCFG3_PWRSMMODESTATUS_SHIFT))
                        ? "ON"
                        : "NOT on");
    cli_write_line(")\r\nGEMPWRSMCFG4 = ");
    cli_report_hex32(DSSREG->GEMPWRSMCFG4);
    cli_write_line(" (halt ");
    cli_write_line(((DSSREG->GEMPWRSMCFG4 & DSSREG_GEMPWRSMCFG4_PWRSMLRSTHALT_BIT) != 0U) ? "asserted" : "released");
    cli_write_line(")\r\n");
}

/* Temporary diagnostic: the first dssLoad attempt against DSS's L2 memory
 * (0x577E0000, per dss_mem.h's translate table) data-aborted on MSS
 * ITSELF (DFAR=0x577E0000) -- the physical bus aliasing the SDK's
 * SOC_translateAddress() LUT documents exists regardless, but Universal_hal's
 * mpu_config() (ported from the mmWave SDK's own SOC_mpu_config(), which
 * never needed MSS to touch DSS's L2 directly) has no MPU region open for
 * it. Before adding a 13th region to mpu_config() (it currently configures
 * exactly 12, indices 0-11), confirm this Cortex-R4F actually implements
 * that many: CP15 c0,c0,4 (MPU Type Register) bits[15:8] = DRegion, the
 * number of MPU regions implemented -- reading this is risk-free (no
 * writes), unlike guessing by writing to a region index that might not
 * exist. */
static void cli_mpu_info(int32_t argc, char* argv[]) {
    uint32_t mpu_type;
    (void)argc;
    (void)argv;

    __asm__ volatile("mrc p15, 0, %0, c0, c0, 4" : "=r"(mpu_type));
    cli_write_line("MPU_TYPE = ");
    cli_report_hex32(mpu_type);
    cli_write_line(" (DRegion = ");
    cli_report_hex32((mpu_type >> 8) & 0xFFU);
    cli_write_line(")\r\n");
}

/* Raw, unrestricted MSS-address read/write -- no DSS-address translation,
 * no range checking, any 32-bit MSS address. Added so that inspecting a
 * NEW address range (a DSS peripheral dss_mem_translate() doesn't know
 * about yet, some other MSS-visible register) never needs new MSS code/a
 * reflash again -- just compute the MSS-side address once (by hand, or
 * via dss_mem_translate()'s own documented ranges in dss_mem.h) and peek/
 * poke it directly. Same pattern GCC_FreeRTOS_VitalSigns_MSS/src/
 * cli_vitalsigns.c's own peek/poke already established. */
static void cli_peek(int32_t argc, char* argv[]) {
    volatile uint32_t* addr;

    if (argc != 2) {
        cli_write_line("Error: usage peek <hexAddr>\r\n");
        return;
    }
    addr = (volatile uint32_t*)cli_parse_hex(argv[1]);
    cli_report_hex32((uint32_t)addr);
    cli_write_line(" = ");
    cli_report_hex32(*addr);
    cli_write_line("\r\n");
}

static void cli_poke(int32_t argc, char* argv[]) {
    volatile uint32_t* addr;
    uint32_t value;

    if (argc != 3) {
        cli_write_line("Error: usage poke <hexAddr> <hexValue>\r\n");
        return;
    }
    addr = (volatile uint32_t*)cli_parse_hex(argv[1]);
    value = cli_parse_hex(argv[2]);
    *addr = value;
    cli_write_line("Done\r\n");
}

/* Raw DSS memory read -- dssAddr is a DSS-side address (same numbers as
 * DSS's own linker script), translated via dss_mem_translate() before the
 * actual access. See dss_mem.h for what's in range. */
static void cli_dss_peek(int32_t argc, char* argv[]) {
    uint32_t dssAddr, mssAddr;

    if (argc != 2) {
        cli_write_line("Error: usage dssPeek <hexDssAddr>\r\n");
        return;
    }
    dssAddr = cli_parse_hex(argv[1]);
    if (!dss_mem_translate(dssAddr, &mssAddr)) {
        cli_write_line("Error: address not in a known DSS L2/L3 range\r\n");
        return;
    }
    cli_report_hex32(dssAddr);
    cli_write_line(" = ");
    cli_report_hex32(*(volatile uint32_t*)mssAddr);
    cli_write_line("\r\n");
}

static void cli_dss_poke(int32_t argc, char* argv[]) {
    uint32_t dssAddr, mssAddr, value;

    if (argc != 3) {
        cli_write_line("Error: usage dssPoke <hexDssAddr> <hexValue>\r\n");
        return;
    }
    dssAddr = cli_parse_hex(argv[1]);
    value = cli_parse_hex(argv[2]);
    if (!dss_mem_translate(dssAddr, &mssAddr)) {
        cli_write_line("Error: address not in a known DSS L2/L3 range\r\n");
        return;
    }
    *(volatile uint32_t*)mssAddr = value;
    cli_write_line("Done\r\n");
}

/* Streams sizeBytes raw bytes from this same UART directly into DSS
 * memory starting at dssAddr, in DSS_LOAD_CHUNK_SIZE chunks (a progress
 * dot per chunk). Validates the WHOLE range up front (both the first and
 * last byte must translate into the same contiguous L2/L3 window) so a
 * mistyped size can't silently spill into unrelated memory partway
 * through a transfer that's already begun consuming UART bytes. */
static void cli_dss_load(int32_t argc, char* argv[]) {
    uint32_t dssAddr, mssAddr, mssEndAddr, sizeBytes, received;

    if (argc != 3) {
        cli_write_line("Error: usage dssLoad <hexDssAddr> <decSizeBytes>\r\n");
        return;
    }
    dssAddr = cli_parse_hex(argv[1]);
    sizeBytes = cli_parse_uint(argv[2]);
    if (sizeBytes == 0U) {
        cli_write_line("Error: size must be > 0\r\n");
        return;
    }
    if (!dss_mem_translate(dssAddr, &mssAddr)) {
        cli_write_line("Error: start address not in a known DSS L2/L3 range\r\n");
        return;
    }
    if (!dss_mem_translate(dssAddr + (sizeBytes - 1U), &mssEndAddr) ||
        (mssEndAddr != (mssAddr + (sizeBytes - 1U)))) {
        cli_write_line("Error: range crosses out of a single DSS L2/L3 window\r\n");
        return;
    }

    cli_write_line("Send ");
    cli_report_hex32(sizeBytes);
    cli_write_line(" raw bytes now...\r\n");

    received = 0U;
    while (received < sizeBytes) {
        uint32_t chunk = sizeBytes - received;
        if (chunk > DSS_LOAD_CHUNK_SIZE) {
            chunk = DSS_LOAD_CHUNK_SIZE;
        }
        (void)uhal_uart_receive(DSS_PROBE_UART, (uint8_t*)(mssAddr + received), chunk);
        received += chunk;
        cli_write_line(".");
    }
    cli_write_line("\r\nDone\r\n");
}

/* Releases the DSP power-domain halt (see Universal_hal's soc_unhalt_dss())
 * so DSS starts executing whatever is currently in its L2 memory from its
 * reset vector -- normally called after one or more dssLoad commands have
 * written a real image there. Calling this when DSS is already running is
 * harmless (it re-clears an already-clear bit and re-polls an
 * already-ON status) but does NOT restart/reset DSS -- see this file's
 * header for why a true restart-with-new-code isn't implemented yet. */
static void cli_dss_start(int32_t argc, char* argv[]) {
    (void)argc;
    (void)argv;

    if (soc_unhalt_dss() == UHAL_STATUS_OK) {
        cli_write_line("Done\r\n");
    } else {
        cli_write_line("Error: DSS never reported powered-on\r\n");
    }
}

/* Full chip warm reset -- same effect as the physical reset button
 * (TOP_RCM.SOFTSYSRST, confirmed against the TRM's own "5.8 68xx Control
 * Registers" section for this exact chip, see AWR6843_TOPRCM.h). Added
 * because dssLoad/dssStart can't restart an already-running DSS (see
 * cli_dss_start()'s own header) -- every DSS test iteration up to now
 * needed a physical reset between runs; this replaces that with a CLI
 * command, so the whole load-test-reset loop can run unattended. Never
 * returns (the chip resets out from under this UART transaction -- the
 * console will need to reconnect after boot, same as after a physical
 * reset). */
static void cli_reset(int32_t argc, char* argv[]) {
    (void)argc;
    (void)argv;

    cli_write_line("Resetting...\r\n");
    TOP_RCM->SOFTSYSRST = TOPRCM_SOFTSYSRST_TRIGGER_VALUE;
    for (;;) {
    }
}

static void cli_help(int32_t argc, char* argv[]) {
    (void)argc;
    (void)argv;
    cli_write_line("Commands:\r\n");
    cli_write_line("  dssStatus\r\n");
    cli_write_line("  mpuInfo\r\n");
    cli_write_line("  peek <hexAddr>  (raw MSS address, no translation)\r\n");
    cli_write_line("  poke <hexAddr> <hexValue>  (raw MSS address, no translation)\r\n");
    cli_write_line("  dssPeek <hexDssAddr>\r\n");
    cli_write_line("  dssPoke <hexDssAddr> <hexValue>\r\n");
    cli_write_line("  dssLoad <hexDssAddr> <decSizeBytes>  (then send raw bytes)\r\n");
    cli_write_line("  dssStart\r\n");
    cli_write_line("  reset  (full chip warm reset, same as the physical reset button)\r\n");
    cli_write_line("  help\r\n");
}

typedef struct {
    const char* name;
    void (*handler)(int32_t argc, char* argv[]);
} cli_command_t;

static const cli_command_t cli_commands[] = {
    {"dssStatus", cli_dss_status},
    {"mpuInfo", cli_mpu_info},
    {"peek", cli_peek},
    {"poke", cli_poke},
    {"dssPeek", cli_dss_peek},
    {"dssPoke", cli_dss_poke},
    {"dssLoad", cli_dss_load},
    {"dssStart", cli_dss_start},
    {"reset", cli_reset},
    {"help", cli_help},
};
#define CLI_NUM_COMMANDS (sizeof(cli_commands) / sizeof(cli_commands[0]))

static void cli_dispatch(int32_t argc, char* argv[]) {
    size_t i;

    for (i = 0; i < CLI_NUM_COMMANDS; i++) {
        if (strcmp(argv[0], cli_commands[i].name) == 0) {
            cli_commands[i].handler(argc, argv);
            return;
        }
    }
    cli_write_line("Error: unknown command (try 'help')\r\n");
}

void vCliTask(void* pvParameters) {
    char line[CLI_LINE_MAX];
    char* argv[CLI_MAX_ARGS];
    (void)pvParameters;

    cli_write_line("\r\nDssProbe:/>");
    for (;;) {
        size_t len = cli_read_line(line, sizeof(line));
        if (len > 0U) {
            int32_t argc = cli_tokenize(line, argv, CLI_MAX_ARGS);
            if (argc > 0) {
                cli_dispatch(argc, argv);
            }
        }
        cli_write_line("DssProbe:/>");
    }
}
