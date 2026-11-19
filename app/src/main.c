#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(demo, LOG_LEVEL_DBG);

#define STACK_SIZE 1024

/* Equal priorities: neither thread can preempt the other, only the
 * time slice (CONFIG_TIMESLICE_SIZE in prj.conf) switches between them.
 */
#define PRIO_A 5
#define PRIO_B 5

#define INCREMENTS    1000
#define RMW_WINDOW_US 100   /* work done between reading and writing counter */

#define MODE_NAME (IS_ENABLED(CONFIG_APP_COUNTER_USE_MUTEX) ? \
                   "mutex-protected" : "unprotected")

static uint32_t counter;
K_MUTEX_DEFINE(counter_lock);

void counter_fn(void *p1, void *p2, void *p3)
{
    for (int i = 0; i < INCREMENTS; i++) {
        if (IS_ENABLED(CONFIG_APP_COUNTER_USE_MUTEX)) {
            k_mutex_lock(&counter_lock, K_FOREVER);
        }

        /*
         * counter++ written out as three steps: read, modify, write.
         *
         * Without the mutex these steps are not atomic. Both threads have
         * the same priority and time slicing is on, so when a thread's
         * 10 ms slice expires the timer interrupt switches to the other
         * thread, and that can happen inside k_busy_wait(), after the
         * read and before the write. Example:
         *
         *   A: tmp = counter              (reads 500)
         *   -- A's slice expires, B runs --
         *   B: increments 100 times       (counter 500 -> 600)
         *   -- B's slice expires, A runs --
         *   A: counter = tmp + 1          (writes 501)
         *
         * A's write is based on a stale copy taken before B's writes, so
         * it overwrites them and B's 100 increments are lost. B does the
         * same to A when it resumes with its own stale tmp.
         *
         * With the mutex, B blocks in k_mutex_lock() while A holds the
         * lock, so no thread can read counter while another one holds an
         * unwritten copy of it, and every increment is kept.
         */
        uint32_t tmp = counter;

        k_busy_wait(RMW_WINDOW_US);
        counter = tmp + 1;

        if (IS_ENABLED(CONFIG_APP_COUNTER_USE_MUTEX)) {
            k_mutex_unlock(&counter_lock);
        }
    }
}

K_THREAD_DEFINE(thread_a, STACK_SIZE, counter_fn,
                NULL, NULL, NULL, PRIO_A, 0, 0);
K_THREAD_DEFINE(thread_b, STACK_SIZE, counter_fn,
                NULL, NULL, NULL, PRIO_B, 0, 0);

int main(void)
{
    const uint32_t expected = 2 * INCREMENTS;

    LOG_INF("mode: %s, %d increments per thread, %d ms time slice",
            MODE_NAME, INCREMENTS, CONFIG_TIMESLICE_SIZE);

    k_thread_join(thread_a, K_FOREVER);
    k_thread_join(thread_b, K_FOREVER);

    /*
     * Measured on native_sim (Zephyr 4.2, 10 ms slice), logs in
     * docs/l2-task1/:
     *
     *   mode              counter  expected  lost
     *   mutex-protected      2000      2000     0
     *   unprotected          1099      2000   901  (45 %)
     *
     * Both builds run the same loop under the same time slicing; the only
     * difference is CONFIG_APP_COUNTER_USE_MUTEX.
     *
     * Mutex-protected: every read-modify-write runs to completion before
     * the other thread may read counter. A slice can still expire inside
     * the critical section, but the other thread then blocks on the lock
     * and A gets the CPU back, so all 2000 increments are counted.
     *
     * Unprotected: at almost every slice switch the preempted thread is
     * holding an unwritten tmp. When it resumes it writes tmp + 1, wiping
     * out the ~100 increments the other thread made during its slice.
     * That repeats over the ~20 slices of the 200 ms run, which is where
     * the 901 missing updates went. The result is below the expected
     * total and is not a count of anything meaningful.
     *
     * native_sim gives 1099 on every run: simulated code takes zero time,
     * so the slice always expires inside k_busy_wait(). On real hardware
     * the number varies from run to run, but stays well below 2000,
     * because the 100 us window covers nearly all of each iteration.
     */
    LOG_INF("counter = %u, expected %u, lost %u updates (%s)",
            counter, expected, expected - counter, MODE_NAME);

    return 0;
}
