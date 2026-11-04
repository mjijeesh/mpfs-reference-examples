#include <string.h>
#include "../common/shared_telemetry.h"

#define CPU_FREQ_HZ 600000000ULL

static inline uint64_t read_cycles(void)
{
    uint64_t c;
    __asm__ volatile ("rdcycle %0" : "=r"(c));
    return c;
}

int main(void)
{
    volatile hart_telemetry_t *t = &TELEMETRY->hart[4];
   telemetry_set_name(t->task_name, "Watchdog & System Timer", sizeof(t->task_name));
    t->state = HART_STATE_STARTING;
    t->heartbeat = 0;
    t->iterations = 0;
    t->errors = 0;
    t->custom_data = 0;

    while (!TELEMETRY->system_ready) {
        memory_barrier();
    }

    t->state = HART_STATE_RUNNING;

    uint64_t last_cycle = read_cycles();
    uint64_t uptime_sec = 0;

    while (1) {
        uint64_t cur_cycle = read_cycles();

        if ((cur_cycle - last_cycle) >= CPU_FREQ_HZ) {
            last_cycle = cur_cycle;
            uptime_sec++;
            t->custom_data = uptime_sec;

            // Monitor peer heartbeats
            for (int h = 1; h <= 3; h++) {
                if (TELEMETRY->hart[h].state == HART_STATE_RUNNING &&
                    TELEMETRY->hart[h].heartbeat == 0) {
                    t->errors++;
                }
            }
        }

        t->iterations++;
        t->heartbeat++;
        memory_barrier();
    }

    return 0;
}