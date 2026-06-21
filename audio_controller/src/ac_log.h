#ifndef AC_LOG_H_
#define AC_LOG_H_

#include "audio_controller.h"

typedef struct ac_log_controller {
    const audio_controller_log_source_ops_t* ops;
    int open;
    int running;
} ac_log_controller_t;

int ac_log_init(ac_log_controller_t* log,
                const audio_controller_log_source_ops_t* ops);
void ac_log_deinit(ac_log_controller_t* log);
int ac_log_start(ac_log_controller_t* log);
void ac_log_stop(ac_log_controller_t* log);
int ac_log_read(ac_log_controller_t* log,
                void* buffer,
                size_t capacity,
                size_t* actual_size,
                unsigned int timeout_ms);

#endif
