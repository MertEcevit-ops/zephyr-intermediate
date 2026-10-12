#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(demo, LOG_LEVEL_DBG);

#define STACK_SIZE 2048

/* Preemptive priorities are >= 0, lower number = higher priority */
#define PRIO_LOW  7
#define PRIO_MED  5
#define PRIO_HIGH 3
/* Negative priorities are cooperative: never preempted by another thread */
#define PRIO_COOP -1

#define COOP_START_DELAY_MS 1000
#define COOP_ITERATIONS     5
#define COOP_BUSY_US        100000
#define COOP_SLEEP_MS       1000

void t_low_fn(void *p1, void *p2, void *p3)
{
    while (1) {
        LOG_INF("T_LOW running");
        k_msleep(300);
    }
}

void t_med_fn(void *p1, void *p2, void *p3)
{
    while (1) {
        LOG_INF("T_MED running");
        k_msleep(200);
    }
}

void t_high_fn(void *p1, void *p2, void *p3)
{
    while (1) {
        LOG_INF("T_HIGH running");
        k_msleep(100);
    }
}

void t_coop_fn(void *p1, void *p2, void *p3)
{
    while (1) {
        /*
         * The other threads' sleeps expire during this busy work, so they
         * become Ready, but none of them can preempt a cooperative thread.
         */
        for (int i = 1; i <= COOP_ITERATIONS; i++) {
            LOG_WRN("T_COOP busy work %d/%d", i, COOP_ITERATIONS);
            k_busy_wait(COOP_BUSY_US);
        }

        /*
         * k_yield() keeps us Ready and only gives way to threads of equal
         * or higher priority. There are none, so we resume immediately.
         */
        LOG_WRN("T_COOP k_yield()");
        k_yield();
        LOG_WRN("T_COOP back from k_yield(), nobody else ran");

        /*
         * k_sleep() moves us to Waiting, so the scheduler picks the highest
         * priority Ready thread and the preemptive threads finally run.
         */
        LOG_WRN("T_COOP k_sleep()");
        k_msleep(COOP_SLEEP_MS);
    }
}

K_THREAD_DEFINE(t_low, STACK_SIZE, t_low_fn,
                NULL, NULL, NULL, PRIO_LOW, 0, 0);
K_THREAD_DEFINE(t_med, STACK_SIZE, t_med_fn,
                NULL, NULL, NULL, PRIO_MED, 0, 0);
K_THREAD_DEFINE(t_high, STACK_SIZE, t_high_fn,
                NULL, NULL, NULL, PRIO_HIGH, 0, 0);
K_THREAD_DEFINE(t_coop, STACK_SIZE, t_coop_fn,
                NULL, NULL, NULL, PRIO_COOP, 0, COOP_START_DELAY_MS);

int main(void)
{
    return 0;
}
