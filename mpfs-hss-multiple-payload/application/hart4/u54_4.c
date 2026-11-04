#include "mpfs_hal/mss_hal.h"
#include "hart_shared.h"
#include <string.h>

#define CPU_FREQ_HZ 600000000ULL /* PolarFire SoC 600 MHz Core Clock */

static inline uint64_t read_cycles(void)
{
    uint64_t c;
    __asm__ volatile ("rdcycle %0" : "=r"(c));
    return c;
}

void u54_4(void)
{
    clear_soft_interrupt();

    hart_telemetry_t *t = &g_hart_shared.hart[4];
    strncpy(t->task_name, "Watchdog & System Timer", sizeof(t->task_name));
    t->state = HART_STATE_STARTING;
    t->heartbeat = 0;
    t->iterations = 0;
    t->errors = 0;
    t->custom_data = 0;

    /* Wait until Hart 1 completes peripheral & system initialization */
    while (!g_hart_shared.system_ready) {
        memory_barrier();
    }

    t->state = HART_STATE_RUNNING;

    uint64_t last_cycle = read_cycles();
    uint64_t seconds_counter = 0;

    while (1) {
        uint64_t cur_cycle = read_cycles();

        /* Track 1-second intervals (600,000,000 CPU cycles) */
        if ((cur_cycle - last_cycle) >= CPU_FREQ_HZ) {
            last_cycle = cur_cycle;
            seconds_counter++;
            t->custom_data = seconds_counter; /* Uptime in seconds */

            /* Deadlock detection: Verify peer harts are incrementing */
            for (int h = 1; h <= 3; h++) {
                if (g_hart_shared.hart[h].state == HART_STATE_RUNNING &&
                    g_hart_shared.hart[h].heartbeat == 0) {
                    t->errors++;
                }
            }
        }

        t->iterations++;
        t->heartbeat++;
        memory_barrier();
    }
}