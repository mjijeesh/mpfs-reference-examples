#ifndef HART_SHARED_H
#define HART_SHARED_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    HART_STATE_STOPPED = 0,
    HART_STATE_STARTING,
    HART_STATE_RUNNING,
    HART_STATE_ERROR
} hart_state_t;

typedef struct {
    volatile uint32_t state;
    volatile uint64_t heartbeat;
    volatile uint64_t iterations;
    volatile uint64_t errors;
    volatile uint64_t custom_data;
    char task_name[32];
} __attribute__((aligned(64))) hart_telemetry_t;

typedef struct {
    volatile bool system_ready;
    hart_telemetry_t hart[5]; // Index 1..4 map to U54 Harts
} __attribute__((aligned(64))) hart_shared_block_t;

extern hart_shared_block_t g_hart_shared;

static inline void memory_barrier(void) {
    __asm__ volatile ("fence rw, rw" ::: "memory");
}

#endif // HART_SHARED_H