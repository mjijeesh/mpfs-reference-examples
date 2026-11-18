#ifndef HART_SHARED_H
#define HART_SHARED_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define SHARED_MAGIC        0x50465335U  /* "PFS5" */
#define EVENT_LOG_SIZE      16U

/* Per-hart control flags driven by E51 CLI */
#define HART_CTRL_NONE          0x00U
#define HART_CTRL_PAUSE         0x01U
#define HART_CTRL_INJECT_FAULT  0x02U
#define HART_CTRL_BENCH_BURST   0x04U

typedef enum {
    HART_STATE_STOPPED = 0,
    HART_STATE_STARTING,
    HART_STATE_RUNNING,
    HART_STATE_PAUSED,
    HART_STATE_ERROR
} hart_state_t;

typedef struct {
    volatile uint32_t state;
    volatile uint32_t ctrl_flags;
    volatile uint64_t heartbeat;
    volatile uint64_t iterations;
    volatile uint64_t errors;
    volatile uint64_t custom_data;
    volatile uint64_t metric_x100;     /* Fixed-point metric * 100 (integer-safe for E51) */
    volatile uint64_t entry_addr;      /* Function entry address (&e51, &u54_1..4) */
    volatile uint64_t exec_pc;         /* Live instruction address (PC) inside worker loop */
    volatile uint64_t stack_ptr;       /* Live private stack pointer (SP) */
    char task_name[32];
} __attribute__((aligned(64))) hart_telemetry_t;

typedef struct {
    volatile uint32_t hart_id;
    volatile uint32_t code;
    volatile uint64_t timestamp_cycles;
    char message[48];
} __attribute__((aligned(64))) hart_event_t;

typedef struct {
    volatile uint32_t magic;
    volatile bool system_ready;
    volatile uint32_t log_head;
    volatile uint32_t log_lock;
    hart_telemetry_t hart[5];          /* Index 0 = E51, Indices 1..4 = U54_1..4 */
    hart_event_t events[EVENT_LOG_SIZE];
} __attribute__((aligned(64))) hart_shared_block_t;

extern hart_shared_block_t g_hart_shared;

static inline void memory_barrier(void)
{
    __asm__ volatile ("fence rw, rw" ::: "memory");
}

static inline uint64_t read_cycles(void)
{
    uint64_t c;
    __asm__ volatile ("rdcycle %0" : "=r"(c));
    return c;
}

/* Capture live PC and private SP of the calling hart */
static inline void record_hart_location(hart_telemetry_t *t)
{
    uint64_t pc_val = 0;
    uint64_t sp_val = 0;
    __asm__ volatile ("auipc %0, 0" : "=r"(pc_val));
    __asm__ volatile ("mv %0, sp"   : "=r"(sp_val));
    t->exec_pc = pc_val;
    t->stack_ptr = sp_val;
}

static inline void shared_log_event(uint32_t hart_id, uint32_t code, const char *msg)
{
    while (__sync_lock_test_and_set(&g_hart_shared.log_lock, 1U) != 0U) {
        /* Spin */
    }

    uint32_t idx = g_hart_shared.log_head % EVENT_LOG_SIZE;
    hart_event_t *ev = &g_hart_shared.events[idx];
    ev->hart_id = hart_id;
    ev->code = code;
    ev->timestamp_cycles = read_cycles();

    size_t i = 0;
    for (; i < sizeof(ev->message) - 1U && msg[i] != '\0'; i++) {
        ev->message[i] = msg[i];
    }
    ev->message[i] = '\0';

    g_hart_shared.log_head++;
    memory_barrier();
    __sync_lock_release(&g_hart_shared.log_lock);
}

#endif /* HART_SHARED_H */