#ifndef AC_TRANSPORT_H_
#define AC_TRANSPORT_H_

#include "ac_datalink.h"
#include "audio_controller.h"

#define AC_TRANSPORT_HEADER_SIZE 36u
#define AC_TRANSPORT_MAX_PAYLOAD \
    (AC_DATALINK_MAX_PACKET_SIZE - AC_TRANSPORT_HEADER_SIZE)
#define AC_TRANSPORT_MAX_CHANNELS 8u
#define AC_TRANSPORT_CHANNEL_QUEUE_DEPTH 4u

#define AC_TRANSPORT_FLAG_REQUEST 0x00000001u
#define AC_TRANSPORT_FLAG_RESPONSE 0x00000002u
#define AC_TRANSPORT_FLAG_ACK 0x00000004u
#define AC_TRANSPORT_FLAG_ERROR 0x00000080u

typedef struct ac_transport_frame {
    uint16_t version;
    uint16_t channel_id;
    uint16_t command_id;
    uint32_t flags;
    uint32_t sequence_id;
    uint32_t session_id;
    const unsigned char* payload;
    size_t payload_size;
} ac_transport_frame_t;

struct ac_transport_controller;
typedef int (*ac_transport_channel_handler_t)(
    void* user,
    struct ac_transport_controller* transport,
    const ac_transport_frame_t* request);

typedef struct ac_transport_queued_request {
    ac_transport_frame_t frame;
    unsigned char payload[AC_TRANSPORT_MAX_PAYLOAD];
} ac_transport_queued_request_t;

typedef struct ac_transport_channel_runtime {
    struct ac_transport_controller* transport;
    uint16_t id;
    const char* name;
    ac_transport_channel_handler_t handler;
    void* handler_user;
    audio_controller_thread_t thread;
    audio_controller_mutex_t mutex;
    int mutex_created;
    int thread_started;
    int open;
    volatile int stop_requested;
    ac_transport_queued_request_t queue[AC_TRANSPORT_CHANNEL_QUEUE_DEPTH];
    size_t head;
    size_t tail;
    size_t count;
} ac_transport_channel_runtime_t;

typedef struct ac_transport_controller {
    ac_datalink_controller_t datalink;
    const audio_controller_driver_ops_t* driver;
    audio_controller_thread_t thread;
    audio_controller_mutex_t io_mutex;
    int io_mutex_created;
    int initialized;
    int running;
    int worker_started;
    volatile int stop_requested;
    ac_transport_channel_runtime_t channels[AC_TRANSPORT_MAX_CHANNELS];
    size_t channel_count;
} ac_transport_controller_t;

int ac_transport_init(ac_transport_controller_t* transport,
                      const audio_controller_driver_ops_t* driver);
int ac_transport_register_channel(ac_transport_controller_t* transport,
                                  uint16_t channel_id,
                                  const char* name,
                                  ac_transport_channel_handler_t handler,
                                  void* handler_user);
int ac_transport_open_channel(ac_transport_controller_t* transport,
                              uint16_t channel_id);
void ac_transport_close_channel(ac_transport_controller_t* transport,
                                uint16_t channel_id);
int ac_transport_start(ac_transport_controller_t* transport);
void ac_transport_deinit(ac_transport_controller_t* transport);
int ac_transport_send_response(ac_transport_controller_t* transport,
                               const ac_transport_frame_t* request,
                               uint32_t extra_flags,
                               const unsigned char* payload,
                               size_t payload_size);
int ac_transport_send_error(ac_transport_controller_t* transport,
                            const ac_transport_frame_t* request,
                            const char* message);
void ac_transport_get_stats(const ac_transport_controller_t* transport,
                            audio_controller_transport_stats_t* stats);

#endif
