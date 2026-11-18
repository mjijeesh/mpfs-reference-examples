#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "board_config.h"
#include "hart_shared.h"

/* Uses &g_mss_uart4_lo from board_config.h (or &g_mss_uart0_lo for HSS port) */
#define CONSOLE_UART        APP_UART

/* PolarFire SoC MSS Soft Reset Control Register */
#define MSS_RESET_CR        (*(volatile uint32_t *)0x20002018UL)
#define MSS_RESET_KEY       0x0000DEADU
#define CLI_BUF_LEN         96U

hart_shared_block_t g_hart_shared = {0};

/* --- Console Output Helpers (RV64IMAC integer-only) --- */

static void cli_putc(char c)
{
    uint8_t ch = (uint8_t)c;
    MSS_UART_polled_tx(CONSOLE_UART, &ch, 1U);
}

static void cli_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') {
            cli_putc('\r');
        }
        cli_putc(*s++);
    }
}

static void cli_put_hex64(uint64_t val, int nibbles)
{
    static const char hex[] = "0123456789ABCDEF";
    for (int i = nibbles - 1; i >= 0; i--) {
        cli_putc(hex[(val >> (i * 4)) & 0xFULL]);
    }
}

static void cli_put_dec(uint64_t val)
{
    char buf[24];
    int idx = 0;
    if (val == 0) {
        cli_putc('0');
        return;
    }
    while (val > 0) {
        buf[idx++] = (char)('0' + (val % 10ULL));
        val /= 10ULL;
    }
    while (idx > 0) {
        cli_putc(buf[--idx]);
    }
}

static bool parse_u64(const char *s, uint64_t *out)
{
    uint64_t val = 0;
    int base = 10;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
    }
    if (*s == '\0') {
        return false;
    }
    while (*s) {
        char c = *s++;
        uint64_t d;
        if (c >= '0' && c <= '9') d = (uint64_t)(c - '0');
        else if (base == 16 && c >= 'a' && c <= 'f') d = (uint64_t)(c - 'a' + 10);
        else if (base == 16 && c >= 'A' && c <= 'F') d = (uint64_t)(c - 'A' + 10);
        else return false;
        val = (val * (uint64_t)base) + d;
    }
    *out = val;
    return true;
}

static const char *state_str(uint32_t st)
{
    switch (st) {
        case HART_STATE_STOPPED:  return "STOPPED ";
        case HART_STATE_STARTING: return "STARTING";
        case HART_STATE_RUNNING:  return "RUNNING ";
        case HART_STATE_PAUSED:   return "PAUSED  ";
        case HART_STATE_ERROR:    return "ERROR!  ";
        default:                  return "UNKNOWN ";
    }
}

/* Decode PolarFire SoC physical address into memory region name (15 chars padded) */
static const char *mpfs_region_name(uint64_t addr)
{
    if (addr >= 0x01000000ULL && addr <= 0x01001FFFULL) return "E51 DTIM       ";
    if (addr >= 0x01800000ULL && addr <= 0x01807FFFULL) return "E51 ITIM       ";
    if (addr >= 0x01808000ULL && addr <= 0x01827FFFULL) return "U54 ITIM       ";
    if (addr >= 0x08000000ULL && addr <= 0x081FFFFFULL) return "L2-LIM (SRAM)  ";
    if (addr >= 0x0A000000ULL && addr <= 0x0A1FFFFFULL) return "L2 Scratchpad  ";
    if (addr >= 0x20220000ULL && addr <= 0x2023FFFFULL) return "eNVM (Flash)   ";
    if (addr >= 0x80000000ULL && addr <= 0xBFFFFFFFULL) return "DDR-Lo Cached  ";
    if (addr >= 0xC0000000ULL && addr <= 0xCFFFFFFFULL) return "DDR-Lo NonCache";
    if (addr >= 0x1000000000ULL && addr <= 0x13FFFFFFFFULL) return "DDR-Hi Cached  ";
    return "Other / MMIO   ";
}

/* --- CLI Command Handlers --- */

static void cmd_help(void)
{
    cli_puts("\n=== PolarFire SoC 5-Core E51 Supervisor CLI ===\n"
             "  help              - Show this menu\n"
             "  info              - Show live 5-core status, PC location & metrics\n"
             "  loc               - Show detailed execution & stack addresses per hart\n"
             "  md <addr> [words] - Memory read (32-bit hex + ASCII dump)\n"
             "  mw <addr> <val>   - Memory write (32-bit hex)\n"
             "  pause <1-4>       - Pause U54 worker hart\n"
             "  resume <1-4>      - Resume U54 worker hart\n"
             "  fault <2|3>       - Inject error on Hart 2 (CRC) or Hart 3 (DDR)\n"
             "  bench             - Trigger 5-core synchronized burst\n"
             "  log               - Display cross-core event log\n"
             "  reset             - Perform MSS hardware soft reset\n\n");
}

static void cmd_loc(void)
{
    cli_puts("\nHart | Core  | Entry Addr | Live PC    | Stack SP   | Execution Region\n"
             "-----+-------+------------+------------+------------+-----------------\n");

    for (uint32_t h = 0; h < 5; h++) {
        hart_telemetry_t *t = &g_hart_shared.hart[h];
        cli_puts("  ");
        cli_put_dec(h);
        cli_puts(h == 0 ? "  | E51   | 0x" : "  | U54_");
        if (h > 0) {
            cli_put_dec(h);
            cli_puts(" | 0x");
        }
        cli_put_hex64(t->entry_addr, 8);
        cli_puts(" | 0x");
        cli_put_hex64(t->exec_pc, 8);
        cli_puts(" | 0x");
        cli_put_hex64(t->stack_ptr, 8);
        cli_puts(" | ");
        cli_puts(mpfs_region_name(t->exec_pc));
        cli_puts("\n");
    }

    cli_puts("\nShared Mailbox (&g_hart_shared): 0x");
    cli_put_hex64((uint64_t)(uintptr_t)&g_hart_shared, 8);
    cli_puts(" (");
    cli_puts(mpfs_region_name((uint64_t)(uintptr_t)&g_hart_shared));
    cli_puts(")\n\n");
}

static void cmd_info(void)
{
    cli_puts("\nHart | Role / Task              | State    | Live PC    | Region          | Heartbeat | Metric   | Err\n"
             "-----+--------------------------+----------+------------+-----------------+-----------+----------+----\n");

    for (uint32_t h = 0; h < 5; h++) {
        hart_telemetry_t *t = &g_hart_shared.hart[h];
        cli_puts("  ");
        cli_put_dec(h);
        cli_puts("  | ");

        size_t len = strlen(t->task_name);
        cli_puts(t->task_name);
        for (size_t p = len; p < 24; p++) cli_putc(' ');

        cli_puts(" | ");
        cli_puts(state_str(t->state));
        cli_puts(" | 0x");
        cli_put_hex64(t->exec_pc, 8);
        cli_puts(" | ");
        cli_puts(mpfs_region_name(t->exec_pc));
        cli_puts(" | ");
        cli_put_hex64(t->heartbeat, 8);
        cli_puts("  | ");

        cli_put_dec(t->metric_x100 / 100ULL);
        cli_putc('.');
        uint64_t frac = t->metric_x100 % 100ULL;
        if (frac < 10ULL) cli_putc('0');
        cli_put_dec(frac);
        cli_puts("\t   | ");

        cli_put_dec(t->errors);
        cli_puts("\n");
    }
    cli_puts("\n");
}

static void cmd_mem_read(uint64_t addr, uint64_t words)
{
    if ((addr & 0x3ULL) != 0ULL) {
        cli_puts("Error: Address must be 4-byte aligned.\n");
        return;
    }
    if (words == 0 || words > 64) {
        words = 16;
    }

    for (uint64_t i = 0; i < words; i += 4) {
        uint64_t line_addr = addr + (i * 4ULL);
        cli_puts("0x");
        cli_put_hex64(line_addr, 8);
        cli_puts(": ");

        uint32_t row[4] = {0};
        for (uint64_t j = 0; j < 4 && (i + j) < words; j++) {
            row[j] = *(volatile uint32_t *)(line_addr + (j * 4ULL));
            cli_put_hex64(row[j], 8);
            cli_putc(' ');
        }

        cli_puts(" |");
        for (uint64_t j = 0; j < 4 && (i + j) < words; j++) {
            for (int b = 0; b < 4; b++) {
                char c = (char)((row[j] >> (b * 8)) & 0xFFU);
                cli_putc((c >= 32 && c <= 126) ? c : '.');
            }
        }
        cli_puts("|\n");
    }
}

static void cmd_mem_write(uint64_t addr, uint64_t val)
{
    if ((addr & 0x3ULL) != 0ULL) {
        cli_puts("Error: Address must be 4-byte aligned.\n");
        return;
    }
    *(volatile uint32_t *)addr = (uint32_t)val;
    memory_barrier();
    cli_puts("Wrote 0x");
    cli_put_hex64((uint32_t)val, 8);
    cli_puts(" -> 0x");
    cli_put_hex64(addr, 8);
    cli_puts("\n");
}

static void cmd_log(void)
{
    uint32_t total = g_hart_shared.log_head;
    uint32_t count = (total < EVENT_LOG_SIZE) ? total : EVENT_LOG_SIZE;
    uint32_t start = (total > EVENT_LOG_SIZE) ? (total - EVENT_LOG_SIZE) : 0U;

    cli_puts("\n--- Cross-Core Event Log ---\n");
    for (uint32_t i = 0; i < count; i++) {
        hart_event_t *ev = &g_hart_shared.events[(start + i) % EVENT_LOG_SIZE];
        cli_puts("[Hart ");
        cli_put_dec(ev->hart_id);
        cli_puts(" @ 0x");
        cli_put_hex64(ev->timestamp_cycles, 10);
        cli_puts("] ");
        cli_puts(ev->message);
        cli_puts("\n");
    }
    cli_puts("\n");
}

static void process_command(char *line)
{
    char *argv[4] = {0};
    int argc = 0;

    char *p = line;
    while (*p && argc < 4) {
        while (*p == ' ' || *p == '\t') *p++ = '\0';
        if (*p == '\0') break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
    }
    if (argc == 0) return;

    if (strcmp(argv[0], "help") == 0) {
        cmd_help();
    } else if (strcmp(argv[0], "info") == 0) {
        cmd_info();
    } else if (strcmp(argv[0], "loc") == 0) {
        cmd_loc();
    } else if (strcmp(argv[0], "md") == 0) {
        uint64_t addr = 0, words = 16;
        if (argc < 2 || !parse_u64(argv[1], &addr)) {
            cli_puts("Usage: md <hex_addr> [word_count]\n");
            return;
        }
        if (argc >= 3) parse_u64(argv[2], &words);
        cmd_mem_read(addr, words);
    } else if (strcmp(argv[0], "mw") == 0) {
        uint64_t addr = 0, val = 0;
        if (argc < 3 || !parse_u64(argv[1], &addr) || !parse_u64(argv[2], &val)) {
            cli_puts("Usage: mw <hex_addr> <hex_val>\n");
            return;
        }
        cmd_mem_write(addr, val);
    } else if (strcmp(argv[0], "pause") == 0 || strcmp(argv[0], "resume") == 0) {
        uint64_t h = 0;
        if (argc < 2 || !parse_u64(argv[1], &h) || h < 1 || h > 4) {
            cli_puts("Usage: pause|resume <1-4>\n");
            return;
        }
        if (argv[0][0] == 'p') {
            g_hart_shared.hart[h].ctrl_flags |= HART_CTRL_PAUSE;
            cli_puts("Paused Hart ");
        } else {
            g_hart_shared.hart[h].ctrl_flags &= ~HART_CTRL_PAUSE;
            cli_puts("Resumed Hart ");
        }
        cli_put_dec(h);
        cli_puts("\n");
    } else if (strcmp(argv[0], "fault") == 0) {
        uint64_t h = 0;
        if (argc < 2 || !parse_u64(argv[1], &h) || (h != 2 && h != 3)) {
            cli_puts("Usage: fault <2|3>\n");
            return;
        }
        g_hart_shared.hart[h].ctrl_flags |= HART_CTRL_INJECT_FAULT;
        cli_puts("Injected single-shot fault on Hart ");
        cli_put_dec(h);
        cli_puts("\n");
    } else if (strcmp(argv[0], "bench") == 0) {
        for (uint32_t h = 1; h <= 4; h++) {
            g_hart_shared.hart[h].ctrl_flags |= HART_CTRL_BENCH_BURST;
        }
        memory_barrier();
        cli_puts("Triggered synchronized burst across U54_1..U54_4. Run 'info' to inspect.\n");
    } else if (strcmp(argv[0], "log") == 0) {
        cmd_log();
    } else if (strcmp(argv[0], "reset") == 0) {
        cli_puts("Triggering PolarFire SoC MSS Reset...\n");
        while ((MSS_UART_get_tx_status(CONSOLE_UART) & MSS_UART_TEMT) == 0U) {}
        memory_barrier();
        MSS_RESET_CR = MSS_RESET_KEY;
        while (1) { __asm__ volatile ("wfi"); }
    } else {
        cli_puts("Unknown command. Type 'help'.\n");
    }
}

void e51(void)
{
    clear_soft_interrupt();
    board_periph_init();

    MSS_UART_init(CONSOLE_UART,
                  MSS_UART_115200_BAUD,
                  MSS_UART_DATA_8_BITS | MSS_UART_NO_PARITY | MSS_UART_ONE_STOP_BIT);

    /* Allow MMUART4 line/baud generator ~5 ms to settle after peripheral reset */
    uint64_t wait_start = read_cycles();
    while ((read_cycles() - wait_start) < 3000000ULL) {}

    memset((void *)&g_hart_shared, 0, sizeof(g_hart_shared));
    g_hart_shared.magic = SHARED_MAGIC;
    

    hart_telemetry_t *t0 = &g_hart_shared.hart[0];
    strncpy(t0->task_name, "E51 Monitor & CLI", sizeof(t0->task_name));
    t0->entry_addr = (uint64_t)(uintptr_t)&e51;
    record_hart_location(t0);
    t0->state = HART_STATE_RUNNING;
    t0->heartbeat = 1;

    memory_barrier();
    g_hart_shared.system_ready = true;
    memory_barrier();

    raise_soft_interrupt(1U);
    raise_soft_interrupt(2U);
    raise_soft_interrupt(3U);
    raise_soft_interrupt(4U);

    shared_log_event(0, 1, "E51 Monitor online; all 5 cores released");
    cmd_help();
    cli_puts("e51> ");

    char cmd_buf[CLI_BUF_LEN];
    size_t pos = 0;

    while (1) {
        record_hart_location(t0);
        t0->heartbeat++;
        t0->iterations++;

        uint8_t rx_byte = 0;
        if (MSS_UART_get_rx(CONSOLE_UART, &rx_byte, 1U) > 0U) {
            char ch = (char)rx_byte;
            if (ch == '\r' || ch == '\n') {
                cli_puts("\n");
                cmd_buf[pos] = '\0';
                process_command(cmd_buf);
                pos = 0;
                cli_puts("e51> ");
            } else if ((ch == '\b' || ch == 0x7F) && pos > 0) {
                pos--;
                cli_puts("\b \b");
            } else if (ch >= 32 && ch <= 126 && pos < (CLI_BUF_LEN - 1U)) {
                cmd_buf[pos++] = ch;
                cli_putc(ch);
            }
        }
    }
}