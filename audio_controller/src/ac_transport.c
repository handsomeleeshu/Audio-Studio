#include "ac_transport.h"

#include <string.h>

static void* ac_transport_worker(void* arg)
{
    ac_transport_controller_t* transport;

    transport = (ac_transport_controller_t*)arg;
    if (!transport)
        return 0;

    while (!transport->stop_requested) {
        (void)ac_datalink_poll(&transport->datalink, 10u);
    }
    return 0;
}

int ac_transport_init(ac_transport_controller_t* transport,
                      const audio_controller_driver_ops_t* driver)
{
    if (!transport || !driver)
        return -1;

    memset(transport, 0, sizeof(*transport));
    if (ac_datalink_init(&transport->datalink, driver->datalink) != 0)
        return -1;

    transport->driver = driver;
    transport->initialized = 1;
    if (driver->datalink && driver->thread_create && driver->thread_join) {
        transport->running = 1;
        if (driver->thread_create(driver->user, &transport->thread,
                                  ac_transport_worker, transport) != 0) {
            transport->running = 0;
            ac_datalink_deinit(&transport->datalink);
            transport->initialized = 0;
            return -1;
        }
        transport->worker_started = 1;
    }
    return 0;
}

void ac_transport_deinit(ac_transport_controller_t* transport)
{
    if (!transport)
        return;

    transport->stop_requested = 1;
    if (transport->worker_started && transport->driver &&
        transport->driver->thread_join) {
        (void)transport->driver->thread_join(transport->driver->user,
                                             transport->thread);
    }
    transport->running = 0;
    transport->worker_started = 0;
    ac_datalink_deinit(&transport->datalink);
    transport->initialized = 0;
}

void ac_transport_get_stats(const ac_transport_controller_t* transport,
                            audio_controller_transport_stats_t* stats)
{
    memset(stats, 0, sizeof(*stats));
    if (!transport)
        return;

    stats->initialized = transport->initialized;
    stats->running = transport->running;
    stats->datalink_open = transport->datalink.open;
    stats->datalink_mtu = (uint32_t)transport->datalink.mtu;
    stats->tx_packets = transport->datalink.tx_packets;
    stats->rx_packets = transport->datalink.rx_packets;
    stats->retries = transport->datalink.retries;
}
