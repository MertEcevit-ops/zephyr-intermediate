#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(demo, LOG_LEVEL_DBG);

#define STACK_SIZE 1024

#define PRIO_A 5
#define PRIO_B 5

#define INCREMENTS 1000

static uint32_t counter;
K_MUTEX_DEFINE(counter_lock);

void counter_fn(void *p1, void *p2, void *p3)
{
    for (int i = 0; i < INCREMENTS; i++) {
        /* Comment out the lock/unlock lines to observe the race */
        k_mutex_lock(&counter_lock, K_FOREVER);
        uint32_t tmp = counter;

        k_busy_wait(100);
        counter = tmp + 1;
        k_mutex_unlock(&counter_lock);
    }
}

K_THREAD_DEFINE(thread_a, STACK_SIZE, counter_fn,
                NULL, NULL, NULL, PRIO_A, 0, 0);
K_THREAD_DEFINE(thread_b, STACK_SIZE, counter_fn,
                NULL, NULL, NULL, PRIO_B, 0, 0);

int main(void)
{
    k_thread_join(thread_a, K_FOREVER);
    k_thread_join(thread_b, K_FOREVER);
    LOG_INF("counter = %u, expected %u", counter, 2 * INCREMENTS);

    return 0;
}
