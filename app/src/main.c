#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/tracing/tracing.h>

LOG_MODULE_REGISTER(homework, LOG_LEVEL_INF);

#define STACK_SIZE            2048

/*
 * Root cause of the delay: with the original CONTROL_PRIORITY of 7, control
 * was BELOW maintenance (4). Each timer event readies both threads at the
 * same instant, so the scheduler always ran maintenance's 45 ms busy wait
 * first while control sat ready in the queue, and control started 45 ms
 * after ready_ms (deadline 10 ms).
 *
 * Fix: control runs at 3, above maintenance, so it is picked first (or
 * preempts maintenance) and starts at once. Maintenance has no short
 * deadline and simply runs after it. Selected with Kconfig so the "before"
 * run uses the same code: -DEXTRA_CONF_FILE=before.conf sets it back to 7.
 * Evidence and write-up: docs/l6-task1/.
 */
#define CONTROL_PRIORITY      CONFIG_APP_CONTROL_PRIORITY   /* 3, was 7 */
#define MAINTENANCE_PRIORITY     4
#define EVENT_PERIOD_MS        250
#define MAINTENANCE_LOAD_US  45000
#define DEADLINE_MS             10   /* control work must start within 10 ms */
#define WARN_INTERVAL_MS      1000

struct control_event {
    uint32_t seq;
    uint32_t ready_ms;
};

K_MSGQ_DEFINE(control_queue, sizeof(struct control_event), 4, 4);
K_SEM_DEFINE(maintenance_start, 0, 1);

/* ================================================================== */
/*  Timer expiry: creates one control event                           */
/* ================================================================== */

static void event_timer_expiry(struct k_timer *timer)
{
    ARG_UNUSED(timer);

    static uint32_t seq;
    struct control_event event = {
        .seq = seq++,
        .ready_ms = k_uptime_get_32(),
    };

    /* Timer expiry runs in interrupt context, so never wait here. */
    int ret = k_msgq_put(&control_queue, &event, K_NO_WAIT);

    if (ret != 0) {
        return;
    }

    /* Both threads become ready when the timer interrupt returns. */
    k_sem_give(&maintenance_start);

    sys_trace_named_event("ctrl_ready", event.seq, event.ready_ms);
}

K_TIMER_DEFINE(event_timer, event_timer_expiry, NULL);

/* ================================================================== */
/*  Control thread                                                   */
/* ================================================================== */

static void control_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    uint32_t misses = 0;
    uint32_t last_warn_ms = 0;

    while (true) {
        struct control_event event;
        int ret = k_msgq_get(&control_queue, &event, K_FOREVER);

        if (ret != 0) {
            LOG_ERR("[CONTROL] receive failed: %d", ret);
            continue;
        }

        /*
         * Guarantee: control work starts within DEADLINE_MS of ready_ms.
         * Measured quantity: start latency = the time this thread got the
         * CPU and took the event, minus ready_ms (stamped by the timer ISR
         * when the event became ready). It is the time control spent ready
         * but not running.
         */
        uint32_t now = k_uptime_get_32();
        uint32_t latency = now - event.ready_ms;

        if (latency > DEADLINE_MS) {
            misses++;
            if (last_warn_ms == 0 || now - last_warn_ms >= WARN_INTERVAL_MS) {
                LOG_WRN("[CONTROL] deadline miss seq=%u latency=%u ms misses=%u/%u",
                        event.seq, latency, misses, event.seq + 1);
                last_warn_ms = now;
            }
        }

        LOG_INF("[CONTROL] processed seq=%u latency=%u ms misses=%u/%u",
                event.seq, latency, misses, event.seq + 1);

        sys_trace_named_event("ctrl_done", event.seq, latency);
    }
}

/* ================================================================== */
/*  Background maintenance thread                                    */
/* ================================================================== */

static void maintenance_fn(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

    while (true) {
        k_sem_take(&maintenance_start, K_FOREVER);

        /* This work is important, but it has no short deadline. */
        k_busy_wait(MAINTENANCE_LOAD_US);
    }
}

K_THREAD_DEFINE(control, STACK_SIZE, control_fn,
                NULL, NULL, NULL, CONTROL_PRIORITY, 0, 0);

K_THREAD_DEFINE(maintenance, STACK_SIZE, maintenance_fn,
                NULL, NULL, NULL, MAINTENANCE_PRIORITY, 0, 0);

int main(void)
{
    LOG_INF("=== L6 Homework: Runtime Investigation ===");
    LOG_INF("Control work must start within 10 ms");
    LOG_INF("Inspect, measure, trace, explain, and correct the delay");
    LOG_INF("control priority %d, maintenance priority %d, load %d us",
            CONTROL_PRIORITY, MAINTENANCE_PRIORITY, MAINTENANCE_LOAD_US);

    k_timer_start(&event_timer, K_MSEC(500), K_MSEC(EVENT_PERIOD_MS));

    return 0;
}
