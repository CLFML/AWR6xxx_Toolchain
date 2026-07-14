/*
 * cli_vitalsigns.c -- hand-rolled UART command-line interface, ported from
 * a reference vital-signs-tracking demo's cli.c command handlers (parse
 * argv -> populate a config struct -> mailbox_write() to DSS). See
 * vitalsigns_mss_testbench.md in the AWR6xxx_Toolchain memory notes for
 * the reference this mimics.
 *
 * This is NOT a port of the reference's CLI framework (TI's
 * ti/utils/cli/cli.c, an SDK component outside this repo) -- just a
 * from-scratch line reader/tokenizer/dispatch table doing the same job.
 * Per-command argv layouts also drop the reference's leading
 * <subFrameIdx> argument: this testbench has no advanced (per-subframe)
 * frame config, so every command always broadcasts with
 * MMWDEMO_SUBFRAME_NUM_FRAME_LEVEL_CONFIG, same as the reference's own
 * vitalSignsCfg/motionDetection/dvsGuiMonitor commands already do.
 *
 * clutterRemoval and guiMonitor (the ODDemo GuiMonSel, distinct from
 * dvsGuiMonitor) are deliberately NOT implemented even though the
 * reference's CLI table has them: the reference DSS build actually
 * flashed as this and every other example's placeholder DSS (this
 * project's prebuilt/placeholder_dss.bin) has no
 * MMWDEMO_MSS2DSS_CLUTTER_REMOVAL case in its mailbox-message switch
 * statement (falls to its default case, which asserts) -- a real bug in
 * the reference fork, not something to reproduce here. guiMonitor is
 * ODDemo (occupancy detection) scope, out of this testbench's vital-signs
 * focus regardless.
 *
 * sensorStart/sensorStop only flip local state (isSensorStarted) and
 * gate nothing else -- the reference's versions call MMWave_open/start/
 * stop (mmWaveLink, driving the actual RF front end over SPI), which this
 * testbench intentionally excludes (see the scope note in main.c). A real
 * DSS in this reference fork never sends MMWDEMO_DSS2MSS_CONFIGDONE either
 * (confirmed by grepping dss_main.c) -- its start/stop completion signal
 * is mmWaveLink's own callback path, not a message this protocol defines.
 * A future custom DSS testbench that wants an explicit "start simulating
 * frames" trigger from MSS will need its own message type for that; it
 * isn't added speculatively here.
 *
 * peek/poke/mboxStatus/dssStatus are debug conveniences, not part of the
 * reference's protocol -- added after real hardware bring-up needed direct
 * register visibility to make progress: `dssStatus` was what confirmed the
 * DSP power-domain bug (see soc_unhalt_dss() in Universal_hal and
 * vitalsigns_mss_testbench.md), and `mboxStatus`/`peek`/`poke` exist for
 * the next mystery of the same kind (currently: DSS demonstrably reads and
 * processes MSS's messages -- it replies with MMWDEMO_DSS2MSS_CONFIGDONE --
 * but MSS's own mailbox_write() still times out waiting for the hardware
 * ack bit on the *original* send, not yet root-caused).
 */
#include <hal_uart.h>
#include <hal_mailbox.h>
#include <mmw_messages.h>
#include <AWR6843.h>
#include <stdbool.h>
#include <string.h>
#include "cli_vitalsigns.h"

/* U16/V16 (MSS_UART_TX/RX) are UARTA, already claimed by main.c for this
 * same command console -- this file only names the peripheral instance,
 * pinmux/uhal_uart_init() happen once in main.c before either FreeRTOS
 * task starts. */
#define MMWDEMO_COMMAND_UART UART_PERIPHERAL_MSS_SCIA

#define CLI_LINE_MAX  128
#define CLI_MAX_ARGS  12

static bool isSensorStarted = false;

static void cli_write_line(const char* msg) {
    uhal_uart_transmit(MMWDEMO_COMMAND_UART, (const uint8_t*)msg, strlen(msg));
}

/* Hand-rolled decimal parsers instead of stdlib.h's atoi()/atof(): this
 * project links with --specs=nosys.specs and no heap section (see
 * linker_awr6843.ld) -- newlib's atof()/strtod() pull in its arbitrary-
 * precision _Balloc()/_Bfree() machinery, which needs malloc()/_sbrk(),
 * which needs a linker-provided `end` symbol this freestanding build
 * doesn't define (confirmed: linking with atof() fails with "undefined
 * reference to `end'" from libnosys's _sbrk stub). atoi() alone would
 * likely have linked fine, but every numeric CLI argument goes through
 * one of these two for consistency. No exponent/scientific-notation
 * support -- not needed for this CLI's plain decimal arguments. */
static int32_t cli_parse_int(const char* s) {
    int32_t result = 0;
    bool negative = false;

    if (*s == '-') {
        negative = true;
        s++;
    } else if (*s == '+') {
        s++;
    }
    while ((*s >= '0') && (*s <= '9')) {
        result = (result * 10) + (int32_t)(*s - '0');
        s++;
    }
    return negative ? -result : result;
}

static float cli_parse_float(const char* s) {
    float result = 0.0f;
    float frac = 0.0f;
    float scale = 0.1f;
    bool negative = false;

    if (*s == '-') {
        negative = true;
        s++;
    } else if (*s == '+') {
        s++;
    }
    while ((*s >= '0') && (*s <= '9')) {
        result = (result * 10.0f) + (float)(*s - '0');
        s++;
    }
    if (*s == '.') {
        s++;
        while ((*s >= '0') && (*s <= '9')) {
            frac += (float)(*s - '0') * scale;
            scale *= 0.1f;
            s++;
        }
    }
    result += frac;
    return negative ? -result : result;
}

/* Hex parser for peek/poke/register-debug commands -- addresses and raw
 * register values are far more natural in hex than decimal. Accepts an
 * optional "0x"/"0X" prefix (not required); no prefix still parses as hex
 * digits, so "50000400" and "0x50000400" both work. */
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
    uhal_uart_transmit(MMWDEMO_COMMAND_UART, buf, sizeof(buf));
}

/* Blocking, echoing line reader. Backspace/DEL erases the last character;
 * \r or \n terminates the line. uhal_uart_receive() busy-waits per byte
 * (see uart_iwr68xx.c) -- fine for a dedicated CLI task: FreeRTOS's
 * tick-driven preemption (configUSE_TIME_SLICING=1) still lets
 * vMboxReadTask run regardless of how long this task spends waiting for
 * the next keystroke. */
static size_t cli_read_line(char* buf, size_t max_len) {
    size_t len = 0;
    uint8_t ch;

    for (;;) {
        (void)uhal_uart_receive(MMWDEMO_COMMAND_UART, &ch, 1);

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
            (void)uhal_uart_transmit(MMWDEMO_COMMAND_UART, &ch, 1);
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

static void cli_send_config(const MmwDemo_message* message) {
    /* mailbox_write() gives up after a bounded wait for DSS's ack rather
     * than blocking forever (see MAILBOX_ACK_TIMEOUT_ITERATIONS in
     * mailbox_iwr68xx.c) -- expected against the real reference DSS binary
     * this testbench doesn't drive through mmWaveLink's cooperative sync
     * (see this file's header), not necessarily a bug on this side. */
    if (mailbox_write(MAILBOX_PERIPHERAL_MSS_DSS, (const uint8_t*)message, sizeof(*message)) == UHAL_STATUS_OK) {
        cli_write_line("Done\r\n");
    } else {
        cli_write_line("Error: DSS did not ack (timed out)\r\n");
    }
}

static void cli_sensor_start(int32_t argc, char* argv[]) {
    (void)argc;
    (void)argv;
    if (isSensorStarted) {
        cli_write_line("Error: sensor already started\r\n");
        return;
    }
    isSensorStarted = true;
    cli_write_line("Done\r\n");
}

static void cli_sensor_stop(int32_t argc, char* argv[]) {
    (void)argc;
    (void)argv;
    if (!isSensorStarted) {
        cli_write_line("Error: sensor not started\r\n");
        return;
    }
    isSensorStarted = false;
    cli_write_line("Done\r\n");
}

/* Ported from the reference's log2Approx() (cli.c) -- used to validate
 * numAvgChirps is a power of two, same as MmwDemo_CLICalibDcRangeSig. */
static uint32_t cli_log2_approx(uint32_t x) {
    uint32_t idx, detectFlag = 0;

    if (x < 2U) {
        return 0U;
    }
    idx = 32U;
    while ((detectFlag == 0U) || (idx == 0U)) {
        if ((x & 0x80000000U) != 0U) {
            detectFlag = 1U;
        }
        x <<= 1U;
        idx--;
    }
    if (x != 0U) {
        idx++;
    }
    return idx;
}

static void cli_calib_dc_range_sig(int32_t argc, char* argv[]) {
    MmwDemo_CalibDcRangeSigCfg cfg;
    MmwDemo_message message;
    uint32_t log2NumAvgChirps;

    if (argc != 5) {
        cli_write_line("Error: usage calibDcRangeSig <enabled> <negBinIdx> <posBinIdx> <numAvgChirps>\r\n");
        return;
    }

    memset(&cfg, 0, sizeof(cfg));
    cfg.enabled = (uint16_t)cli_parse_int(argv[1]);
    cfg.negativeBinIdx = (int16_t)cli_parse_int(argv[2]);
    cfg.positiveBinIdx = (int16_t)cli_parse_int(argv[3]);
    cfg.numAvgChirps = (uint16_t)cli_parse_int(argv[4]);

    if (cfg.negativeBinIdx > 0) {
        cli_write_line("Error: negative bin index must be <= 0\r\n");
        return;
    }
    if ((cfg.positiveBinIdx - cfg.negativeBinIdx + 1) > DC_RANGE_SIGNATURE_COMP_MAX_BIN_SIZE) {
        cli_write_line("Error: number of bins exceeds the limit\r\n");
        return;
    }
    log2NumAvgChirps = cli_log2_approx(cfg.numAvgChirps);
    if (cfg.numAvgChirps != (1U << log2NumAvgChirps)) {
        cli_write_line("Error: numAvgChirps must be a power of two\r\n");
        return;
    }

    memset(&message, 0, sizeof(message));
    message.type = MMWDEMO_MSS2DSS_CALIB_DC_RANGE_SIG;
    message.subFrameNum = MMWDEMO_SUBFRAME_NUM_FRAME_LEVEL_CONFIG;
    message.body.calibDcRangeSigCfg = cfg;
    cli_send_config(&message);
}

static void cli_adcbuf_cfg(int32_t argc, char* argv[]) {
    MmwDemo_ADCBufCfg cfg;
    MmwDemo_message message;

    if (argc != 5) {
        cli_write_line("Error: usage adcbufCfg <adcFmt> <iqSwapSel> <chInterleave> <chirpThreshold>\r\n");
        return;
    }

    memset(&cfg, 0, sizeof(cfg));
    cfg.adcFmt = (uint8_t)cli_parse_int(argv[1]);
    cfg.iqSwapSel = (uint8_t)cli_parse_int(argv[2]);
    cfg.chInterleave = (uint8_t)cli_parse_int(argv[3]);
    cfg.chirpThreshold = (uint8_t)cli_parse_int(argv[4]);

    memset(&message, 0, sizeof(message));
    message.type = MMWDEMO_MSS2DSS_ADCBUFCFG;
    message.subFrameNum = MMWDEMO_SUBFRAME_NUM_FRAME_LEVEL_CONFIG;
    message.body.adcBufCfg = cfg;
    cli_send_config(&message);
}

static void cli_vital_signs_cfg(int32_t argc, char* argv[]) {
    VitalSignsDemo_ParamsCfg cfg;
    MmwDemo_message message;

    if (argc != 10) {
        cli_write_line("Error: usage vitalSignsCfg <startRange_m> <endRange_m> <winLenBreath> "
                        "<winLenHeart> <rxAntennaProcess> <alphaBreath> <alphaHeart> <scaleBreath> "
                        "<scaleHeart>\r\n");
        return;
    }

    memset(&cfg, 0, sizeof(cfg));
    cfg.startRange_m = (float)cli_parse_float(argv[1]);
    cfg.endRange_m = (float)cli_parse_float(argv[2]);
    cfg.winLen_breathing = (uint16_t)cli_parse_int(argv[3]);
    cfg.winLen_heartRate = (uint16_t)cli_parse_int(argv[4]);
    cfg.rxAntennaProcess = (float)cli_parse_float(argv[5]);
    cfg.alpha_breathingWfm = (float)cli_parse_float(argv[6]);
    cfg.alpha_heartWfm = (float)cli_parse_float(argv[7]);
    cfg.scale_breathingWfm = (float)cli_parse_float(argv[8]);
    cfg.scale_heartWfm = (float)cli_parse_float(argv[9]);

    memset(&message, 0, sizeof(message));
    message.type = MMWDEMO_MSS2DSS_VITALSIGNS_MEASUREMENT_PARAMS;
    message.subFrameNum = MMWDEMO_SUBFRAME_NUM_FRAME_LEVEL_CONFIG;
    message.body.vitalSignsParamsCfg = cfg;
    cli_send_config(&message);
}

static void cli_motion_detection(int32_t argc, char* argv[]) {
    VitalSignsDemo_MotionDetection cfg;
    MmwDemo_message message;

    if (argc != 5) {
        cli_write_line("Error: usage motionDetection <enabled> <blockSize> <threshold> <gainControl>\r\n");
        return;
    }

    memset(&cfg, 0, sizeof(cfg));
    cfg.enabled = (uint16_t)cli_parse_int(argv[1]);
    cfg.blockSize = (uint16_t)cli_parse_int(argv[2]);
    cfg.threshold = (float)cli_parse_float(argv[3]);
    cfg.gainControl = (uint16_t)cli_parse_int(argv[4]);

    memset(&message, 0, sizeof(message));
    message.type = MMWDEMO_MSS2DSS_VITALSIGNS_MOTION_DETECTION;
    message.subFrameNum = MMWDEMO_SUBFRAME_NUM_FRAME_LEVEL_CONFIG;
    message.body.motionDetectionParamsCfg = cfg;
    cli_send_config(&message);
}

static void cli_dvs_gui_monitor(int32_t argc, char* argv[]) {
    VitalSignsDemo_GuiMonSel cfg;
    MmwDemo_message message;

    if (argc != 6) {
        cli_write_line("Error: usage dvsGuiMonitor <Param1> <Param2> <ClutterRemoval> <Reset> <statsInfo>\r\n");
        return;
    }

    memset(&cfg, 0, sizeof(cfg));
    cfg.guiFlag_Param1 = (uint8_t)cli_parse_int(argv[1]);
    cfg.guiFlag_Param2 = (uint8_t)cli_parse_int(argv[2]);
    cfg.guiFlag_ClutterRemoval = (uint8_t)cli_parse_int(argv[3]);
    cfg.guiFlag_Reset = (uint8_t)cli_parse_int(argv[4]);
    cfg.statsInfo = (uint8_t)cli_parse_int(argv[5]);

    memset(&message, 0, sizeof(message));
    message.type = MMWDEMO_VITALSIGNS_GUIMON_CFG;
    message.subFrameNum = MMWDEMO_SUBFRAME_NUM_FRAME_LEVEL_CONFIG;
    message.body.vitalSigns_GuiMonSel = cfg;
    cli_send_config(&message);
}

/* Raw memory read -- any MSS-address-space address, not just DSS-related
 * ones (though that's the main reason this exists: seeing DSSREG/mailbox
 * register contents directly instead of guessing from symptoms alone,
 * e.g. this is exactly what confirmed the DSP power-domain bug -- see
 * vitalsigns_mss_testbench.md in the AWR6xxx_Toolchain memory notes). No
 * bounds/alignment checking: a bad address data-aborts the same as any
 * other invalid access would, reported by freertos_glue.c's fault
 * handler same as always. */
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

/* Raw memory write -- see cli_peek()'s header for the why/risk. */
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

/* Raw mailbox doorbell register dump for both MSS<->DSS link directions --
 * see AWR6843_MAILBOX.h for what each field means. Built specifically to
 * debug the "DSS processed the message and replied, but MSS's own
 * mailbox_write() still timed out waiting for the ack bit" symptom: this
 * shows the actual hardware state (INT_STS_RAW vs INT_STS_MASKED, whether
 * INT_MASK is gating something) instead of guessing from mailbox_write()'s
 * pass/fail alone. */
static void cli_mbox_status(int32_t argc, char* argv[]) {
    (void)argc;
    (void)argv;

    cli_write_line("MSS->DSS (MBOX_MSS_DSS_REG):\r\n");
    cli_write_line("  INT_MASK       = ");
    cli_report_hex32(MBOX_MSS_DSS_REG->INT_MASK);
    cli_write_line("\r\n  INT_STS_MASKED = ");
    cli_report_hex32(MBOX_MSS_DSS_REG->INT_STS_MASKED);
    cli_write_line("\r\n  INT_STS_RAW    = ");
    cli_report_hex32(MBOX_MSS_DSS_REG->INT_STS_RAW);
    cli_write_line("\r\nDSS->MSS (MBOX_DSS_MSS_REG):\r\n");
    cli_write_line("  INT_MASK       = ");
    cli_report_hex32(MBOX_DSS_MSS_REG->INT_MASK);
    cli_write_line("\r\n  INT_STS_MASKED = ");
    cli_report_hex32(MBOX_DSS_MSS_REG->INT_STS_MASKED);
    cli_write_line("\r\n  INT_STS_RAW    = ");
    cli_report_hex32(MBOX_DSS_MSS_REG->INT_STS_RAW);
    cli_write_line("\r\n");
}

/* DSP power-domain status + a fresh read of the alive-check magic value
 * (see mbox_task.c's mbox_dss_alive_check(), which only runs once at
 * vMboxReadTask startup -- this lets you re-check on demand, e.g. after a
 * suspected DSS crash/reset later in a session). GEMPWRSMCFG3 bits
 * [19:18]: 3 = DSP power domain ON (see AWR6843_DSSREG.h). */
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
    cli_write_line("\r\nalive-check magic = ");
    cli_report_hex32(*(volatile uint32_t*)MMWDEMO_DSS_ALIVE_CHECK_ADDRESS_MSS);
    cli_write_line(" (expect ");
    cli_report_hex32(MMWDEMO_DSS_ALIVE_CHECK_MAGIC);
    cli_write_line(" if DSS's CPU has run its init task)\r\n");
}

static void cli_help(int32_t argc, char* argv[]) {
    (void)argc;
    (void)argv;
    cli_write_line("Commands:\r\n");
    cli_write_line("  sensorStart\r\n");
    cli_write_line("  sensorStop\r\n");
    cli_write_line("  calibDcRangeSig <enabled> <negBinIdx> <posBinIdx> <numAvgChirps>\r\n");
    cli_write_line("  adcbufCfg <adcFmt> <iqSwapSel> <chInterleave> <chirpThreshold>\r\n");
    cli_write_line("  vitalSignsCfg <startRange_m> <endRange_m> <winLenBreath> <winLenHeart> "
                    "<rxAntennaProcess> <alphaBreath> <alphaHeart> <scaleBreath> <scaleHeart>\r\n");
    cli_write_line("  motionDetection <enabled> <blockSize> <threshold> <gainControl>\r\n");
    cli_write_line("  dvsGuiMonitor <Param1> <Param2> <ClutterRemoval> <Reset> <statsInfo>\r\n");
    cli_write_line("  peek <hexAddr>\r\n");
    cli_write_line("  poke <hexAddr> <hexValue>\r\n");
    cli_write_line("  mboxStatus\r\n");
    cli_write_line("  dssStatus\r\n");
    cli_write_line("  help\r\n");
}

typedef struct {
    const char* name;
    void (*handler)(int32_t argc, char* argv[]);
} cli_command_t;

static const cli_command_t cli_commands[] = {
    {"sensorStart", cli_sensor_start},
    {"sensorStop", cli_sensor_stop},
    {"calibDcRangeSig", cli_calib_dc_range_sig},
    {"adcbufCfg", cli_adcbuf_cfg},
    {"vitalSignsCfg", cli_vital_signs_cfg},
    {"motionDetection", cli_motion_detection},
    {"dvsGuiMonitor", cli_dvs_gui_monitor},
    {"peek", cli_peek},
    {"poke", cli_poke},
    {"mboxStatus", cli_mbox_status},
    {"dssStatus", cli_dss_status},
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

    cli_write_line("\r\nVitalSignsTestbench:/>");
    for (;;) {
        size_t len = cli_read_line(line, sizeof(line));
        if (len > 0U) {
            int32_t argc = cli_tokenize(line, argv, CLI_MAX_ARGS);
            if (argc > 0) {
                cli_dispatch(argc, argv);
            }
        }
        cli_write_line("VitalSignsTestbench:/>");
    }
}
