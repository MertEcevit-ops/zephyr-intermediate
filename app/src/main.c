#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/task_wdt/task_wdt.h>

LOG_MODULE_REGISTER(demo, LOG_LEVEL_DBG);

#define STACK_SIZE  1024
#define QUEUE_SIZE  8
#define PRODUCE_MS  100
#define WDT_MS      500
#define HEALTH_MS   200
#define STUCK_AT    10      /* consumer gets stuck on this sample */
#define STUCK_MS    3000

K_MSGQ_DEFINE(sample_q, sizeof(uint32_t), QUEUE_SIZE, 4);

static void producer_fn(void *p1, void *p2, void *p3)
{
    uint32_t sample = 0;

    while (1) {
        sample++;
        if (k_msgq_put(&sample_q, &sample, K_NO_WAIT) != 0) {
            LOG_ERR("[PRODUCER] queue full, sample %u dropped", sample);
        }
        k_msleep(PRODUCE_MS);
    }
}

/* Runs in interrupt context when the consumer stops feeding its channel */
static void wdt_cb(int channel_id, void *user_data)
{
    LOG_ERR("[WDT] channel %d expired, thread %s is stuck", channel_id,
            k_thread_name_get(user_data));
}

static void consumer_fn(void *p1, void *p2, void *p3)
{
    int wdt_id = task_wdt_add(WDT_MS, wdt_cb, k_current_get());
    uint32_t sample;

    while (1) {
        k_msgq_get(&sample_q, &sample, K_FOREVER);
        task_wdt_feed(wdt_id);
        LOG_INF("[CONSUMER] sample %u", sample);

        if (sample == STUCK_AT) {
            LOG_WRN("[CONSUMER] simulating stuck for %d ms", STUCK_MS);
            k_msleep(STUCK_MS);
        }
    }
}

static void health_fn(void *p1, void *p2, void *p3)
{
    while (1) {
        uint32_t used = k_msgq_num_used_get(&sample_q);

        if (used * 100 >= QUEUE_SIZE * 75) {
            LOG_WRN("[HEALTH] queue at %u/%d", used, QUEUE_SIZE);
        }
        k_msleep(HEALTH_MS);
    }
}

K_THREAD_DEFINE(producer, STACK_SIZE, producer_fn, NULL, NULL, NULL, 5, 0, 0);
K_THREAD_DEFINE(consumer, STACK_SIZE, consumer_fn, NULL, NULL, NULL, 5, 0, 0);
K_THREAD_DEFINE(health, STACK_SIZE, health_fn, NULL, NULL, NULL, 6, 0, 0);

static int wdt_init(void)
{
    return task_wdt_init(NULL);
}

SYS_INIT(wdt_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
