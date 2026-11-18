#include "mpfs_hal/mss_hal.h"
#include "hart_shared.h"
#include <string.h>

#define CPU_FREQ_HZ 600000000ULL /* PolarFire SoC 600 MHz Core Clock */

void u54_4(void)
{
    clear_soft_interrupt();

    while (g_hart_shared.magic != SHARED_MAGIC || !g_hart_shared.system_ready) {
        memory_barrier();
    }

    hart_telemetry_t *t = &g_hart_shared.hart[4];
    strncpy(t->task_name, "5-Core Watchdog & Timer", sizeof(t->task_name));
    t->entry_addr = (uint64_t)(uintptr_t)&u54_4;
    t->state = HART_STATE_RUNNING;

    uint64_t last_cycle = read_cycles();
    uint64_t seconds_counter = 0;
    uint64_t prev_hb[5] = {0};

    for (int h = 0; h < 5; h++) {
        prev_hb[h] = g_hart_shared.hart[h].heartbeat;
    }

    while (1) {

        record_hart_location(t);
        uint64_t cur_cycle = read_cycles();

        if ((cur_cycle - last_cycle) >= CPU_FREQ_HZ) {
            last_cycle = cur_cycle;
            seconds_counter++;
            t->custom_data = seconds_counter;
            t->metric_x100 = seconds_counter * 100ULL; /* Uptime in seconds */

            /* Monitor Hart 0 (E51) and Harts 1..3 for heartbeat stalls */
            for (int h = 0; h <= 3; h++) {
                hart_telemetry_t *peer = &g_hart_shared.hart[h];
                uint64_t cur_hb = peer->heartbeat;

                if (peer->state == HART_STATE_RUNNING && cur_hb == prev_hb[h]) {
                    peer->state = HART_STATE_ERROR;
                    t->errors++;
                    shared_log_event(4, 0xE4, "WATCHDOG: Peer hart heartbeat stalled!");
                }
                prev_hb[h] = cur_hb;
            }
        }

        t->iterations++;
        t->heartbeat++;
        memory_barrier();
    }
}