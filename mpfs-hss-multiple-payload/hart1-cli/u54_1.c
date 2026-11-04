#include "mpfs_hal/mss_hal.h"
#include "uart.h"
#include "cli.h"
#include "hart_shared.h"
#include <string.h>

// Allocate global shared telemetry block
hart_shared_block_t g_hart_shared = {0};
#include "../common/shared_telemetry.h"

void u54_1(void)
{
    uart_init();
    clear_soft_interrupt();

    // Initialize the shared structure at fixed address
    memset((void *)TELEMETRY, 0, sizeof(shared_telemetry_block_t));

    volatile hart_telemetry_t *t = &TELEMETRY->hart[1];
    strncpy((char *)t->task_name, "Interactive CLI Master", sizeof(t->task_name));
    t->state = HART_STATE_RUNNING;
    t->heartbeat = 1;

    memory_barrier();
    TELEMETRY->system_ready = true;
    memory_barrier();

    cli_init();
    cli_run();
}