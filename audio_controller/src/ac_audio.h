#ifndef AC_AUDIO_H_
#define AC_AUDIO_H_

#include "audio_controller.h"
#include "ac_transport_channel.h"

#include <stddef.h>
#include <stdint.h>

struct ac_transport_controller;
struct ac_transport_frame;
struct sof_stream;

#define AC_AUDIO_STREAM_NAME_SIZE 32u
#define AC_AUDIO_DMA_ALIGNMENT 4096u

typedef struct ac_audio_stream_config {
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bytes_per_sample;
} ac_audio_stream_config_t;

typedef struct ac_audio_stream_slot {
    uint32_t id;
    uint32_t direction;
    char stream_name[AC_AUDIO_STREAM_NAME_SIZE];
    struct sof_stream* stream;
    ac_audio_stream_config_t config;
    uint16_t data_channel_id;
    uint16_t frame_bytes;
    const audio_controller_driver_ops_t* driver;
    int allocated;
    int configured;
    int running;
    int stream_started;
    int channel_registered;
    void* dma_raw;
    void* dma_aligned;
    size_t dma_buffer_size;
    size_t frames_written;
    size_t frames_read;
} ac_audio_stream_slot_t;

typedef struct ac_audio_controller {
    ac_audio_stream_slot_t streams[AC_TRANSPORT_AUDIO_MAX_STREAMS];
    const audio_controller_driver_ops_t* driver;
} ac_audio_controller_t;

int ac_audio_init(ac_audio_controller_t* audio,
                  const audio_controller_driver_ops_t* driver);
void ac_audio_deinit(ac_audio_controller_t* audio);
int ac_audio_listen(ac_audio_controller_t* audio,
                    struct ac_transport_controller* transport);
int ac_audio_open(ac_audio_controller_t* audio,
                  uint32_t direction,
                  const char* stream_name,
                  uint32_t* stream_id);
int ac_audio_configure(ac_audio_controller_t* audio,
                       uint32_t stream_id,
                       const ac_audio_stream_config_t* config);
int ac_audio_start(ac_audio_controller_t* audio,
                   struct ac_transport_controller* transport,
                   uint32_t stream_id,
                   uint16_t* data_channel_id);
int ac_audio_stop(ac_audio_controller_t* audio, uint32_t stream_id);
int ac_audio_close(ac_audio_controller_t* audio, uint32_t stream_id);
int ac_audio_control_transport_handler(void* user,
                                       struct ac_transport_controller* transport,
                                       const struct ac_transport_frame* request);
int ac_audio_data_transport_handler(void* user,
                                    struct ac_transport_controller* transport,
                                    const struct ac_transport_frame* request);

#endif
