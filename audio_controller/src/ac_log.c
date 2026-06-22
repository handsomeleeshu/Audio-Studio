#include "ac_log.h"

#include "ac_transport.h"
#include "ac_transport_channel.h"

#include <string.h>

#define AC_LOG_TRANSPORT_READ_CHUNK 512u

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

int ac_log_listen(ac_log_controller_t* log,
                  struct ac_transport_controller* transport)
{
    if (!log || !transport)
        return -1;
    if (ac_transport_register_channel(transport, AC_TRANSPORT_CHANNEL_LOG,
                                      "log", ac_log_transport_handler,
                                      log) != 0)
        return -1;
    return ac_transport_open_channel(transport, AC_TRANSPORT_CHANNEL_LOG);
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

int ac_log_transport_handler(void* user,
                             struct ac_transport_controller* transport,
                             const struct ac_transport_frame* request)
{
    ac_log_controller_t* log;
    unsigned char payload[AC_LOG_TRANSPORT_READ_CHUNK];
    size_t payload_size;

    log = (ac_log_controller_t*)user;
    if (!log || !transport || !request)
        return -1;

    if (request->command_id == AC_TRANSPORT_LOG_OPEN) {
        if (ac_log_start(log) != 0)
            return ac_transport_send_error(transport, request,
                                           "log start failed");
        return ac_transport_send_response(transport, request, 0u, 0, 0u);
    }

    if (request->command_id == AC_TRANSPORT_LOG_READ) {
        if (ac_log_read(log, payload, sizeof(payload), &payload_size,
                        100u) < 0)
            return ac_transport_send_error(transport, request,
                                           "log read failed");
        return ac_transport_send_response(transport, request, 0u, payload,
                                          payload_size);
    }

    if (request->command_id == AC_TRANSPORT_LOG_CLOSE) {
        ac_log_stop(log);
        return ac_transport_send_response(transport, request, 0u, 0, 0u);
    }

    return ac_transport_send_error(transport, request, "bad log command");
}
