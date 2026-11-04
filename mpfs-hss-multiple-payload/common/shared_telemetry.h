#ifndef SHARED_TELEMETRY_H
#define SHARED_TELEMETRY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define SHARED_TELEMETRY_BASE 0x83F00000UL

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
    hart_telemetry_t hart[5]; // Indices 1..4 map to U54 Harts
} __attribute__((aligned(64))) shared_telemetry_block_t;

#define TELEMETRY ((volatile shared_telemetry_block_t *)SHARED_TELEMETRY_BASE)

static inline void memory_barrier(void) {
    __asm__ volatile ("fence rw, rw" ::: "memory");
}

/* Freestanding string copy - no libc / strncpy dependency */
static inline void telemetry_set_name(volatile char *dst, const char *src, size_t maxlen)
{
    size_t i = 0;
    while (src[i] != '\0' && i < (maxlen - 1)) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

#endif // SHARED_TELEMETRY_H