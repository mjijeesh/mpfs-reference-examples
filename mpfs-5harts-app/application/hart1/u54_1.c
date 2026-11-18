#include "mpfs_hal/mss_hal.h"
#include "hart_shared.h"
#include <string.h>

#define MAT_N 8
#define CPU_FREQ_HZ 600000000ULL

static double mat_a[MAT_N][MAT_N];
static double mat_b[MAT_N][MAT_N];
static double mat_c[MAT_N][MAT_N];

void u54_1(void)
{
    clear_soft_interrupt();

    while (g_hart_shared.magic != SHARED_MAGIC || !g_hart_shared.system_ready) {
        memory_barrier();
    }

    hart_telemetry_t *t = &g_hart_shared.hart[1];
    strncpy(t->task_name, "FPU Matrix DSP (RV64GC)", sizeof(t->task_name));
    t->entry_addr = (uint64_t)(uintptr_t)&u54_1;
    t->state = HART_STATE_RUNNING;

    for (int i = 0; i < MAT_N; i++) {
        for (int j = 0; j < MAT_N; j++) {
            mat_a[i][j] = (double)(i + 1) * 0.25;
            mat_b[i][j] = (double)(j + 1) * 0.125;
        }
    }

    shared_log_event(1, 0, "U54_1 FPU Matrix DSP initialized");

    while (1) {

        record_hart_location(t);
        if (t->ctrl_flags & HART_CTRL_PAUSE) {
            t->state = HART_STATE_PAUSED;
            t->heartbeat++;
            continue;
        }
        t->state = HART_STATE_RUNNING;

        uint32_t loops = (t->ctrl_flags & HART_CTRL_BENCH_BURST) ? 5000U : 500U;
        t->ctrl_flags &= ~HART_CTRL_BENCH_BURST;

        uint64_t t0 = read_cycles();
        for (uint32_t rep = 0; rep < loops; rep++) {
            for (int i = 0; i < MAT_N; i++) {
                for (int j = 0; j < MAT_N; j++) {
                    double sum = 0.0;
                    for (int k = 0; k < MAT_N; k++) {
                        sum += mat_a[i][k] * mat_b[k][j];
                    }
                    mat_c[i][j] = sum;
                }
            }
            mat_a[0][0] += 0.0001;
        }
        uint64_t dt = read_cycles() - t0;

        /* 2 * N^3 FLOPs per matrix multiply = 1024 FLOPs */
        if (dt > 0) {
            uint64_t total_flops = (uint64_t)loops * 1024ULL;
            /* MFLOPS * 100 = (total_flops * CPU_FREQ_HZ * 100) / (dt * 1e6) */
            t->metric_x100 = (total_flops * 60000ULL) / dt;
        }

        t->custom_data = (uint64_t)mat_c[MAT_N - 1][MAT_N - 1];
        t->iterations++;
        t->heartbeat++;
        memory_barrier();
    }
}