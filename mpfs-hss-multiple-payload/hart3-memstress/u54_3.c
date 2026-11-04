#include "mpfs_hal/mss_hal.h"
#include "hart_shared.h"
#include <string.h>

#define STRESS_BUF_WORDS 4096
static volatile uint32_t s_stress_buffer[STRESS_BUF_WORDS] __attribute__((aligned(64)));

void u54_3(void)
{
    clear_soft_interrupt();

    hart_telemetry_t *t = &g_hart_shared.hart[3];
    strncpy(t->task_name, "Memory Stress (DDR Bus)", sizeof(t->task_name));
    t->state = HART_STATE_STARTING;
    t->heartbeat = 0;
    t->iterations = 0;
    t->errors = 0;

    while (!g_hart_shared.system_ready) {
        memory_barrier();
    }

    t->state = HART_STATE_RUNNING;

    while (1) {
        uint32_t pattern = 0x55AA55AAUL ^ (uint32_t)t->iterations;

        // Phase 1: Write pattern
        for (size_t i = 0; i < STRESS_BUF_WORDS; i++) {
            s_stress_buffer[i] = pattern + (uint32_t)i;
        }
        memory_barrier();

        // Phase 2: Read & Verify
        for (size_t i = 0; i < STRESS_BUF_WORDS; i++) {
            uint32_t expected = pattern + (uint32_t)i;
            if (s_stress_buffer[i] != expected) {
                t->errors++;
            }
        }

        t->custom_data = (uint64_t)&s_stress_buffer[0];
        t->iterations++;
        t->heartbeat++;
        memory_barrier();
    }
}