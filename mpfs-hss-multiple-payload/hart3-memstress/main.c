#include <string.h>
#include "../common/shared_telemetry.h"

#define TEST_BUF_WORDS 8192
static volatile uint32_t s_test_buffer[TEST_BUF_WORDS] __attribute__((aligned(64)));

int main(void)
{
    volatile hart_telemetry_t *t = &TELEMETRY->hart[3];
    strncpy((char *)t->task_name, "Memory Stress (DDR Bus)", sizeof(t->task_name));
    t->state = HART_STATE_STARTING;
    t->heartbeat = 0;
    t->iterations = 0;
    t->errors = 0;

    while (!TELEMETRY->system_ready) {
        memory_barrier();
    }

    t->state = HART_STATE_RUNNING;

    while (1) {
        uint32_t pattern = 0xAA55AA55UL ^ (uint32_t)t->iterations;

        // Phase 1: Write walking pattern
        for (size_t i = 0; i < TEST_BUF_WORDS; i++) {
            s_test_buffer[i] = pattern + (uint32_t)i;
        }
        memory_barrier();

        // Phase 2: Read & Verify
        for (size_t i = 0; i < TEST_BUF_WORDS; i++) {
            uint32_t expected = pattern + (uint32_t)i;
            if (s_test_buffer[i] != expected) {
                t->errors++;
            }
        }

        t->custom_data = (uint64_t)&s_test_buffer[0];
        t->iterations++;
        t->heartbeat++;
        memory_barrier();
    }

    return 0;
}