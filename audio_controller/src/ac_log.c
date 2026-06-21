#include "ac_log.h"

#include <string.h>

int ac_log_init(ac_log_controller_t* log,
                const audio_controller_log_source_ops_t* ops)
{
    if (!log)
        return -1;

    memset(log, 0, sizeof(*log));
    log->ops = ops;
    if (!ops)
        return 0;
    if (!ops->read)
        return -1;
    return 0;
}

void ac_log_deinit(ac_log_controller_t* log)
{
    if (!log)
        return;

    ac_log_stop(log);
    if (log->open && log->ops && log->ops->close)
        log->ops->close(log->ops->user);
    memset(log, 0, sizeof(*log));
}

int ac_log_start(ac_log_controller_t* log)
{
    if (!log || !log->ops)
        return 0;
    if (!log->open) {
        if (log->ops->open && log->ops->open(log->ops->user) != 0)
            return -1;
        log->open = 1;
    }
    if (!log->running && log->ops->start &&
        log->ops->start(log->ops->user) != 0)
        return -1;
    log->running = 1;
    return 0;
}

void ac_log_stop(ac_log_controller_t* log)
{
    if (!log || !log->ops || !log->running)
        return;
    if (log->ops->stop)
        log->ops->stop(log->ops->user);
    log->running = 0;
}

int ac_log_read(ac_log_controller_t* log,
                void* buffer,
                size_t capacity,
                size_t* actual_size,
                unsigned int timeout_ms)
{
    if (!actual_size)
        return -1;
    *actual_size = 0u;
    if (!log || !log->ops)
        return 1;
    if (!log->running)
        return 1;
    if (!log->ops->read)
        return -1;
    return log->ops->read(log->ops->user, buffer, capacity, actual_size,
                          timeout_ms);
}
