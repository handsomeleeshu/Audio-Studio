#ifndef AC_TRANSPORT_H_
#define AC_TRANSPORT_H_

#include "ac_datalink.h"

#define AC_TRANSPORT_LOG_BUFFER_SIZE 8192u

typedef struct ac_transport_controller {
    ac_datalink_controller_t datalink;
    const audio_controller_driver_ops_t* driver;
    audio_controller_thread_t thread;
    audio_controller_mutex_t log_mutex;
    int log_mutex_created;
    unsigned char log_buffer[AC_TRANSPORT_LOG_BUFFER_SIZE];
    size_t log_head;
    size_t log_size;
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
int ac_transport_append_log_data(ac_transport_controller_t* transport,
                                 const void* data,
                                 size_t size);

#endif
