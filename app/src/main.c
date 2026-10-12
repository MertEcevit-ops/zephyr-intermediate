/*
 * Lecture 3 - Homework
 *
 * GOAL: Convert a polling loop to an event-driven workqueue architecture.
 *
 * sensor_sim submits a k_work item on every event, so the handler runs
 * only when there is a real event (no polling thread, no wasted wake-ups).
 *
 * BONUS (debounce): set BONUS_DEBOUNCE to 1. sensor_sim then fires a burst
 * of 5 events within 20ms and k_work_reschedule collapses each burst into
 * ONE handler call, 30ms after the last event of the burst.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(homework, LOG_LEVEL_DBG);

#define BONUS_DEBOUNCE 0

#define STACK_SIZE    1024
#define SENSOR_MS     100    /* sensor fires every 100ms */
#define EVENT_COUNT   10     /* total sensor events to produce */
#define BURST_COUNT   5      /* bonus: events per burst */
#define BURST_GAP_US  4000   /* bonus: 5 events span 16ms */
#define DEBOUNCE_MS   30

/* Statistics */
static int total_events;
static int total_processed;

/* ------------------------------------------------------------------ */
/*  sensor_handler - runs on the system workqueue, once per submit    */
/* ------------------------------------------------------------------ */

static void sensor_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    total_processed++;
    LOG_INF("[HANDLER] processed event %d  tick=%u",
            total_processed, k_uptime_get_32());
}

K_WORK_DEFINE(sensor_work, sensor_handler);
K_WORK_DELAYABLE_DEFINE(debounce_work, sensor_handler);

/* ------------------------------------------------------------------ */
/*  sensor_sim - fires EVENT_COUNT events, 100ms apart               */
/* ------------------------------------------------------------------ */

static void sensor_sim_fn(void *p1, void *p2, void *p3)
{
    for (int i = 0; i < EVENT_COUNT; i++) {
        k_msleep(SENSOR_MS);

#if BONUS_DEBOUNCE
        for (int b = 0; b < BURST_COUNT; b++) {
            total_events++;
            LOG_INF("[SENSOR] event %d.%d reschedule  tick=%u", i, b,
                    k_uptime_get_32());

            /* Each call pushes the deadline back, so only the last one fires */
            k_work_reschedule(&debounce_work, K_MSEC(DEBOUNCE_MS));
            k_busy_wait(BURST_GAP_US);
        }
#else
        total_events++;
        LOG_INF("[SENSOR] event %d  tick=%u", i, k_uptime_get_32());

        int ret = k_work_submit(&sensor_work);

        if (ret < 0) {
            LOG_ERR("submit failed: %d", ret);
        }
#endif
    }

    LOG_INF("[SENSOR] all events produced");
}

K_THREAD_DEFINE(sensor_thread, STACK_SIZE, sensor_sim_fn, NULL, NULL, NULL, 5, 0, 0);

int main(void)
{
    LOG_INF("=== L3 Homework: Polling to Workqueue ===");

    /* Wait long enough for all events to complete */
    k_msleep((EVENT_COUNT + 2) * SENSOR_MS + 500);

    LOG_INF("[SUMMARY] events=%d  handler_runs=%d", total_events, total_processed);

    return 0;
}
