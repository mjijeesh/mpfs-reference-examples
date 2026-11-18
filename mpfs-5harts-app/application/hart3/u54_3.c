#include "mpfs_hal/mss_hal.h"
#include "hart_shared.h"
#include <string.h>

#define STRESS_BUF_WORDS 4096U
#define CPU_FREQ_HZ      600000000ULL

static volatile uint32_t s_stress_buffer[STRESS_BUF_WORDS] __attribute__((aligned(64)));

void u54_3(void)
{
    clear_soft_interrupt();

    while (g_hart_shared.magic != SHARED_MAGIC || !g_hart_shared.system_ready) {
        memory_barrier();
    }

    hart_telemetry_t *t = &g_hart_shared.hart[3];
    strncpy(t->task_name, "DDR Bus Stress (MB/s)", sizeof(t->task_name));
    t->entry_addr = (uint64_t)(uintptr_t)&u54_3;
    t->state = HART_STATE_RUNNING;

    while (1) {

        record_hart_location(t);
        if (t->ctrl_flags & HART_CTRL_PAUSE) {
            t->state = HART_STATE_PAUSED;
            t->heartbeat++;
            continue;
        }
        t->state = HART_STATE_RUNNING;

        uint32_t passes = (t->ctrl_flags & HART_CTRL_BENCH_BURST) ? 32U : 4U;
        t->ctrl_flags &= ~HART_CTRL_BENCH_BURST;

        uint64_t t0 = read_cycles();
        for (uint32_t p = 0; p < passes; p++) {
            uint32_t pattern = 0x55AA55AAUL ^ (uint32_t)(t->iterations + p);

            for (size_t i = 0; i < STRESS_BUF_WORDS; i++) {
                s_stress_buffer[i] = pattern + (uint32_t)i;
            }
            memory_barrier();

            if (t->ctrl_flags & HART_CTRL_INJECT_FAULT) {
                t->ctrl_flags &= ~HART_CTRL_INJECT_FAULT;
                s_stress_buffer[128] ^= 0x00010000UL;
            }

            for (size_t i = 0; i < STRESS_BUF_WORDS; i++) {
                if (s_stress_buffer[i] != (pattern + (uint32_t)i)) {
                    t->errors++;
                    shared_log_event(3, 0xE3, "DDR verification bit-error caught!");
                    break;
                }
            }
        }
        uint64_t dt = read_cycles() - t0;

        if (dt > 0) {
            /* Read + Write bytes = 2 * passes * 16 KB */
            uint64_t total_bytes = (uint64_t)passes * STRESS_BUF_WORDS * sizeof(uint32_t) * 2ULL;
            /* MB/s * 100 */
            t->metric_x100 = (total_bytes * CPU_FREQ_HZ * 100ULL) / (dt * 1024ULL * 1024ULL);
        }

        t->custom_data = (uint64_t)&s_stress_buffer[0];
        t->iterations++;
        t->heartbeat++;
        memory_barrier();
    }
}