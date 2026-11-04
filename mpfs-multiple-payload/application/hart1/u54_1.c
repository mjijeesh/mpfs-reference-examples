#include "mpfs_hal/mss_hal.h"
#include "uart.h"
#include "cli.h"
#include "hart_shared.h"
#include <string.h>

// Allocate global shared telemetry block
hart_shared_block_t g_hart_shared = {0};

void u54_1(void)
{
    // 1. Initialize primary UART peripheral
    uart_init();
    clear_soft_interrupt();

    // 2. Set Hart 1 Telemetry
    hart_telemetry_t *t = &g_hart_shared.hart[1];
    strncpy(t->task_name, "Interactive CLI Master", sizeof(t->task_name));
    t->state = HART_STATE_RUNNING;
    t->heartbeat = 1;

    // 3. Release Harts 2, 3, and 4 from barrier
    memory_barrier();
    g_hart_shared.system_ready = true;
    memory_barrier();

    // 4. Run CLI loop
    cli_init();
    cli_run();

    /* Never reached */
}