#ifndef AC_TRANSPORT_H_
#define AC_TRANSPORT_H_

#include "ac_datalink.h"
#include "ac_log.h"

typedef struct ac_transport_controller {
    ac_datalink_controller_t datalink;
    ac_log_controller_t log;
    const audio_controller_driver_ops_t* driver;
    audio_controller_thread_t thread;
    int initialized;
    int running;
    int worker_started;
    volatile int stop_requested;
} ac_transport_controller_t;

int ac_transport_init(ac_transport_controller_t* transport,
                      const audio_controller_driver_ops_t* driver);
void ac_transport_deinit(ac_transport_controller_t* transport);
void ac_transport_get_stats(const ac_transport_controller_t* transport,
                            audio_controller_transport_stats_t* stats);

#endif
