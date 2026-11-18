#include "mpfs_hal/mss_hal.h"
#include "hart_shared.h"
#include <string.h>

#define CPU_FREQ_HZ 600000000ULL

static uint32_t compute_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320U & -(crc & 1U));
        }
    }
    return ~crc;
}

void u54_2(void)
{
    clear_soft_interrupt();

    while (g_hart_shared.magic != SHARED_MAGIC || !g_hart_shared.system_ready) {
        memory_barrier();
    }

    hart_telemetry_t *t = &g_hart_shared.hart[2];
    strncpy(t->task_name, "CRC32 Engine (KB/s)", sizeof(t->task_name));
    t->entry_addr = (uint64_t)(uintptr_t)&u54_2;
    t->state = HART_STATE_RUNNING;

    uint8_t buffer[256];
    for (size_t i = 0; i < sizeof(buffer); i++) {
        buffer[i] = (uint8_t)(i ^ 0x5A);
    }

    while (1) {

        record_hart_location(t);
        if (t->ctrl_flags & HART_CTRL_PAUSE) {
            t->state = HART_STATE_PAUSED;
            t->heartbeat++;
            continue;
        }
        t->state = HART_STATE_RUNNING;

        uint32_t batches = (t->ctrl_flags & HART_CTRL_BENCH_BURST) ? 4000U : 1000U;
        t->ctrl_flags &= ~HART_CTRL_BENCH_BURST;

        buffer[0] = (uint8_t)(t->iterations & 0xFFU);
        uint32_t golden = compute_crc32(buffer, sizeof(buffer));

        if (t->ctrl_flags & HART_CTRL_INJECT_FAULT) {
            t->ctrl_flags &= ~HART_CTRL_INJECT_FAULT;
            buffer[10] ^= 0xFFU; /* Corrupt payload */
            uint32_t bad = compute_crc32(buffer, sizeof(buffer));
            if (bad != golden) {
                t->errors++;
                shared_log_event(2, 0xE2, "CRC32 mismatch detected & caught!");
            }
            buffer[10] ^= 0xFFU; /* Restore */
        }

        uint64_t t0 = read_cycles();
        uint32_t result = 0;
        for (uint32_t i = 0; i < batches; i++) {
            result = compute_crc32(buffer, sizeof(buffer));
        }
        uint64_t dt = read_cycles() - t0;

        if (dt > 0) {
            /* Throughput in KB/s * 100 */
            uint64_t bytes = (uint64_t)batches * sizeof(buffer);
            t->metric_x100 = (bytes * CPU_FREQ_HZ * 100ULL) / (dt * 1024ULL);
        }

        t->custom_data = result;
        t->iterations++;
        t->heartbeat++;
        memory_barrier();
    }
}