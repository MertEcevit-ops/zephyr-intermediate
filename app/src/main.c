#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

LOG_MODULE_REGISTER(demo, LOG_LEVEL_DBG);

#define STACK_SIZE 1024
#define SENSOR_MS  100
#define LOGGER_MS  30    /* simulated slow logging work */

struct sensor_msg {
    uint32_t count;
    int32_t temperature;
};

/* Listener: callback runs in the publisher's context, must be fast */
static void display_cb(const struct zbus_channel *chan)
{
    const struct sensor_msg *msg = zbus_chan_const_msg(chan);

    LOG_INF("[DISPLAY] sample %u  temp=%d", msg->count, msg->temperature);
}

ZBUS_LISTENER_DEFINE(display_lis, display_cb);

/* Subscriber: gets notified through a queue and works in its own thread */
ZBUS_SUBSCRIBER_DEFINE(logger_sub, 4);

ZBUS_CHAN_DEFINE(sensor_chan, struct sensor_msg, NULL, NULL,
                 ZBUS_OBSERVERS(display_lis, logger_sub), ZBUS_MSG_INIT(0));

static void logger_fn(void *p1, void *p2, void *p3)
{
    const struct zbus_channel *chan;
    struct sensor_msg msg;

    while (zbus_sub_wait(&logger_sub, &chan, K_FOREVER) == 0) {
        zbus_chan_read(chan, &msg, K_FOREVER);
        k_msleep(LOGGER_MS);
        LOG_INF("[LOGGER]  sample %u  temp=%d  tick=%u", msg.count,
                msg.temperature, k_uptime_get_32());
    }
}

K_THREAD_DEFINE(logger_thread, STACK_SIZE, logger_fn, NULL, NULL, NULL, 5, 0, 0);

int main(void)
{
    struct sensor_msg msg = {0};

    while (1) {
        msg.count++;
        msg.temperature = 20 + msg.count % 10;
        zbus_chan_pub(&sensor_chan, &msg, K_MSEC(SENSOR_MS));
        k_msleep(SENSOR_MS);
    }

    return 0;
}
