#include "mpfs_hal/mss_hal.h"
#include "hart_shared.h"
#include <string.h>

static uint32_t compute_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}

void u54_2(void)
{
    clear_soft_interrupt();

    hart_telemetry_t *t = &g_hart_shared.hart[2];
    strncpy(t->task_name, "Compute (CRC32 Engine)", sizeof(t->task_name));
    t->state = HART_STATE_STARTING;
    t->heartbeat = 0;
    t->iterations = 0;
    t->errors = 0;

    // Wait until Hart 1 completes peripheral & system initialization
    while (!g_hart_shared.system_ready) {
        memory_barrier();
    }

    t->state = HART_STATE_RUNNING;

    uint8_t buffer[256];
    for (size_t i = 0; i < sizeof(buffer); i++) {
        buffer[i] = (uint8_t)(i ^ 0x5A);
    }

    while (1) {
        // Workload: 1000 CRC iterations per cycle
        uint32_t result = 0;
        for (int i = 0; i < 1000; i++) {
            buffer[0] = (uint8_t)(t->iterations & 0xFF);
            result = compute_crc32(buffer, sizeof(buffer));
        }

        t->custom_data = result;
        t->iterations++;
        t->heartbeat++;
        memory_barrier();
    }
}