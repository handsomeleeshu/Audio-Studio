#include "ac_audio.h"

#include "ac_transport.h"
#include "sof-stream.h"

#include <string.h>

#define AC_AUDIO_START_RESPONSE_SIZE 8u
#define AC_AUDIO_OPEN_RESPONSE_SIZE 4u
#define AC_AUDIO_OPEN_MIN_REQUEST_SIZE 4u
#define AC_AUDIO_CONFIG_REQUEST_SIZE 12u
#define AC_AUDIO_STREAM_ID_REQUEST_SIZE 4u
#define AC_AUDIO_READ_REQUEST_SIZE 4u
#define AC_AUDIO_STREAM_WAIT_SLEEP_MS 1u
#define AC_AUDIO_STREAM_WAIT_RETRIES 5000u
#define AC_AUDIO_DEFAULT_PERIOD_US 4000u
#define AC_AUDIO_DEFAULT_PERIOD_COUNT 4u
#define AC_AUDIO_DRAIN_FLUSH_PERIOD_COUNT 5u
#define AC_AUDIO_HOST_PAGE_COUNT 2u

static uint16_t ac_audio_read_le16(const unsigned char* data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t ac_audio_read_le32(const unsigned char* data)
{
    return ((uint32_t)data[0]) |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static void ac_audio_write_le16(unsigned char* data, uint16_t value)
{
    data[0] = (unsigned char)(value & 0xffu);
    data[1] = (unsigned char)((value >> 8) & 0xffu);
}

static void ac_audio_write_le32(unsigned char* data, uint32_t value)
{
    data[0] = (unsigned char)(value & 0xffu);
    data[1] = (unsigned char)((value >> 8) & 0xffu);
    data[2] = (unsigned char)((value >> 16) & 0xffu);
    data[3] = (unsigned char)((value >> 24) & 0xffu);
}

static void ac_audio_copy_name(char* dst, size_t dst_size,
                               const char* src, size_t src_size)
{
    size_t i;

    if (!dst || dst_size == 0u)
        return;
    if (!src)
        src_size = 0u;

    i = 0u;
    while (i + 1u < dst_size && i < src_size && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int ac_audio_valid_direction(uint32_t direction)
{
    return direction == AC_TRANSPORT_AUDIO_DIRECTION_PLAYBACK ||
           direction == AC_TRANSPORT_AUDIO_DIRECTION_CAPTURE;
}

static ac_audio_stream_slot_t*
ac_audio_find_stream(ac_audio_controller_t* audio, uint32_t stream_id)
{
    size_t i;

    if (!audio || stream_id == 0u)
        return 0;
    for (i = 0u; i < AC_TRANSPORT_AUDIO_MAX_STREAMS; ++i) {
        if (audio->streams[i].allocated &&
            audio->streams[i].id == stream_id)
            return &audio->streams[i];
    }
    return 0;
}

static ac_audio_stream_slot_t*
ac_audio_find_stream_by_channel(ac_audio_controller_t* audio,
                                uint16_t channel_id)
{
    size_t i;

    if (!audio)
        return 0;
    for (i = 0u; i < AC_TRANSPORT_AUDIO_MAX_STREAMS; ++i) {
        if (audio->streams[i].allocated &&
            audio->streams[i].data_channel_id == channel_id)
            return &audio->streams[i];
    }
    return 0;
}

static void ac_audio_release_sof_stream(ac_audio_stream_slot_t* slot)
{
    if (!slot)
        return;
    if (slot->stream) {
        (void)sof_stream_close(slot->stream);
        slot->stream = 0;
    }
    slot->configured = 0;
    slot->frame_bytes = 0u;
    slot->stream_started = 0;
}

static void ac_audio_set_default_chmap(uint16_t* chmap, uint16_t channels)
{
    unsigned int i;
    static const uint16_t defaults[] = {
        SOF_CHMAP_FL,
        SOF_CHMAP_FR,
        SOF_CHMAP_FC,
        SOF_CHMAP_LFE,
        SOF_CHMAP_RL,
        SOF_CHMAP_RR,
        SOF_CHMAP_SL,
        SOF_CHMAP_SR,
    };

    for (i = 0u; i < SOF_IPC_MAX_CHANNELS; ++i)
        chmap[i] = SOF_CHMAP_UNKNOWN;
    if (channels == 1u) {
        chmap[0] = SOF_CHMAP_MONO;
        return;
    }
    for (i = 0u; i < channels && i < SOF_IPC_MAX_CHANNELS; ++i) {
        if (i < sizeof(defaults) / sizeof(defaults[0]))
            chmap[i] = defaults[i];
        else
            chmap[i] = SOF_CHMAP_UNKNOWN;
    }
}

static int ac_audio_frame_format(uint16_t bytes_per_sample, uint32_t* frame_fmt)
{
    if (!frame_fmt)
        return -1;
    if (bytes_per_sample == 2u) {
        *frame_fmt = SOF_IPC_FRAME_S16_LE;
        return 0;
    }
    if (bytes_per_sample == 4u) {
        *frame_fmt = SOF_IPC_FRAME_S32_LE;
        return 0;
    }
    return -1;
}

static void ac_audio_free_dma(ac_audio_stream_slot_t* slot)
{
    if (!slot)
        return;
    if (slot->dma_raw && slot->driver && slot->driver->free)
        slot->driver->free(slot->driver->user, slot->dma_raw);
    slot->dma_raw = 0;
    slot->dma_aligned = 0;
    slot->dma_buffer_size = 0u;
}

static int ac_audio_prepare_dma(ac_audio_stream_slot_t* slot,
                                uint32_t frame_bytes,
                                struct sof_stream_params* params)
{
#if !CONFIG_HOST_PTABLE
    uint64_t period_bytes64;
    uint32_t period_bytes;
#endif
    uint32_t dma_buf_size;
    uintptr_t aligned_addr;
    size_t alloc_size;

    if (!slot || !params || frame_bytes == 0u)
        return -1;
    if (!slot->driver || !slot->driver->alloc || !slot->driver->free)
        return -1;

#if CONFIG_HOST_PTABLE
    dma_buf_size = AC_AUDIO_HOST_PAGE_COUNT * AC_AUDIO_DMA_ALIGNMENT;
#else
    period_bytes64 = ((uint64_t)slot->config.sample_rate *
                      (uint64_t)AC_AUDIO_DEFAULT_PERIOD_US *
                      (uint64_t)frame_bytes + 1000000ULL - 1ULL) /
                     1000000ULL;
    if (period_bytes64 == 0ULL || period_bytes64 > 0xffffffffULL)
        return -1;
    period_bytes = (uint32_t)period_bytes64;
    period_bytes = (period_bytes + frame_bytes - 1u) / frame_bytes *
                   frame_bytes;

    dma_buf_size = period_bytes * AC_AUDIO_DEFAULT_PERIOD_COUNT;
    if (dma_buf_size < period_bytes)
        return -1;
    dma_buf_size = (dma_buf_size + AC_AUDIO_DMA_ALIGNMENT - 1u) &
                   ~(AC_AUDIO_DMA_ALIGNMENT - 1u);
    params->host_period_bytes = period_bytes;
#endif

    ac_audio_free_dma(slot);
    alloc_size = (size_t)dma_buf_size + AC_AUDIO_DMA_ALIGNMENT - 1u;
    slot->dma_raw = slot->driver->alloc(slot->driver->user, alloc_size,
                                        AC_AUDIO_DMA_ALIGNMENT);
    if (!slot->dma_raw)
        return -1;

    aligned_addr = ((uintptr_t)slot->dma_raw + AC_AUDIO_DMA_ALIGNMENT - 1u) &
                   ~((uintptr_t)AC_AUDIO_DMA_ALIGNMENT - 1u);
    slot->dma_aligned = (void*)aligned_addr;
    slot->dma_buffer_size = dma_buf_size;

    params->dma_buffer.size = dma_buf_size;
    params->dma_buffer.pages =
        (dma_buf_size + AC_AUDIO_DMA_ALIGNMENT - 1u) /
        AC_AUDIO_DMA_ALIGNMENT;
    params->dma_buffer.addr = slot->dma_aligned;
    return 0;
}

static void ac_audio_reset_slot(ac_audio_stream_slot_t* slot)
{
    uint32_t id;
    uint16_t data_channel_id;
    const audio_controller_driver_ops_t* driver;
    int channel_registered;

    if (!slot)
        return;

    ac_audio_free_dma(slot);
    id = slot->id;
    data_channel_id = slot->data_channel_id;
    driver = slot->driver;
    channel_registered = slot->channel_registered;
    memset(slot, 0, sizeof(*slot));
    slot->id = id;
    slot->data_channel_id = data_channel_id;
    slot->driver = driver;
    slot->channel_registered = channel_registered;
}

static int ac_audio_prepare_sof_stream(ac_audio_stream_slot_t* slot)
{
    struct sof_stream_params params;
    uint32_t frame_fmt;
    uint32_t frame_bytes;

    if (!slot || !slot->allocated)
        return -1;
    if (slot->config.sample_rate == 0u || slot->config.channels == 0u ||
        slot->config.bytes_per_sample == 0u)
        return -2;
    if (ac_audio_frame_format(slot->config.bytes_per_sample, &frame_fmt) != 0)
        return -3;

    frame_bytes = (uint32_t)slot->config.channels *
                  (uint32_t)slot->config.bytes_per_sample;
    if (frame_bytes == 0u || frame_bytes > 65535u)
        return -4;

    if (!slot->stream) {
        slot->stream = sof_stream_open(slot->stream_name);
        if (!slot->stream)
            return -5;
    }

    memset(&params, 0, sizeof(params));
    params.frame_fmt = frame_fmt;
    params.buffer_fmt = SOF_IPC_BUFFER_INTERLEAVED;
    params.rate = slot->config.sample_rate;
    params.channels = slot->config.channels;
    params.host_period_bytes = 1u;
    params.cont_update_posn = 1u;
    params.long_unwrap_posn = 1u;
    ac_audio_set_default_chmap(params.chmap, slot->config.channels);
    if (ac_audio_prepare_dma(slot, frame_bytes, &params) != 0) {
        ac_audio_release_sof_stream(slot);
        return -6;
    }

    if (sof_stream_config(slot->stream, &params) != 0) {
        ac_audio_free_dma(slot);
        ac_audio_release_sof_stream(slot);
        return -7;
    }

    slot->frame_bytes = (uint16_t)frame_bytes;
    slot->configured = 1;
    return 0;
}

static const char* ac_audio_config_error_message(int ret)
{
    switch (ret) {
    case -1:
        return "audio config failed: stream slot is not allocated";
    case -2:
        return "audio config failed: invalid stream parameters";
    case -3:
        return "audio config failed: unsupported sample format";
    case -4:
        return "audio config failed: invalid frame size";
    case -5:
        return "audio config failed: stream open failed";
    case -6:
        return "audio config failed: dma prepare failed";
    case -7:
        return "audio config failed: sof stream config failed";
    default:
        return "audio config failed";
    }
}

static void ac_audio_transport_sleep(ac_transport_controller_t* transport,
                                     unsigned int milliseconds)
{
    if (transport && transport->driver && transport->driver->sleep_ms)
        transport->driver->sleep_ms(transport->driver->user, milliseconds);
}

static int ac_audio_wait_stream_progress(ac_audio_stream_slot_t* slot,
                                         ac_transport_controller_t* transport,
                                         unsigned int* wait_retries)
{
    if (!slot || !slot->stream || !wait_retries)
        return -1;
    if (*wait_retries >= AC_AUDIO_STREAM_WAIT_RETRIES)
        return -1;
    if (sof_stream_poll_position(slot->stream) != 0)
        return -1;
    (*wait_retries)++;
    ac_audio_transport_sleep(transport, AC_AUDIO_STREAM_WAIT_SLEEP_MS);
    return 0;
}

static int ac_audio_write_stream(ac_audio_stream_slot_t* slot,
                                 ac_transport_controller_t* transport,
                                 const unsigned char* payload,
                                 size_t payload_size);

static int ac_audio_flush_pipeline_tail(ac_audio_stream_slot_t* slot,
                                        ac_transport_controller_t* transport)
{
    uint64_t period_bytes;
    size_t flush_bytes;
    unsigned char* silence;
    int ret;

    period_bytes = (uint64_t)slot->config.sample_rate * slot->frame_bytes *
                   AC_AUDIO_DEFAULT_PERIOD_US / 1000000u;
    flush_bytes = (size_t)period_bytes * AC_AUDIO_DRAIN_FLUSH_PERIOD_COUNT;
    if (flush_bytes == 0u)
        return 0;

    silence = slot->driver->alloc(slot->driver->user, flush_bytes,
                                  slot->frame_bytes);
    if (!silence)
        return -1;
    memset(silence, 0, flush_bytes);
    ret = ac_audio_write_stream(slot, transport, silence, flush_bytes);
    slot->driver->free(slot->driver->user, silence);
    return ret;
}

static int ac_audio_drain_stream(ac_audio_stream_slot_t* slot,
                                 ac_transport_controller_t* transport)
{
    uint64_t buffer_frames;
    uint64_t tail_ms;
    unsigned int wait_retries;
    int32_t free_size;

    if (!slot || !slot->stream || slot->direction !=
        AC_TRANSPORT_AUDIO_DIRECTION_PLAYBACK ||
        slot->dma_buffer_size == 0u || slot->frame_bytes == 0u ||
        slot->config.sample_rate == 0u)
        return -1;
    if (!slot->stream_started)
        return 0;
    if (ac_audio_flush_pipeline_tail(slot, transport) != 0)
        return -1;

    wait_retries = 0u;
    for (;;) {
        free_size = sof_stream_get_free_size(slot->stream);
        if (free_size >= 0 &&
            (size_t)free_size >= slot->dma_buffer_size) {
            buffer_frames = slot->dma_buffer_size / slot->frame_bytes;
            tail_ms = (buffer_frames * 1000u + slot->config.sample_rate - 1u) /
                      slot->config.sample_rate;
            ac_audio_transport_sleep(transport, (unsigned int)tail_ms);
            return 0;
        }
        if (ac_audio_wait_stream_progress(slot, transport,
                                          &wait_retries) != 0)
            return -1;
    }
}

int ac_audio_init(ac_audio_controller_t* audio,
                  const audio_controller_driver_ops_t* driver)
{
    size_t i;

    if (!audio || !driver || !driver->alloc || !driver->free)
        return -1;
    memset(audio, 0, sizeof(*audio));
    audio->driver = driver;
    for (i = 0u; i < AC_TRANSPORT_AUDIO_MAX_STREAMS; ++i) {
        audio->streams[i].id = (uint32_t)i + 1u;
        audio->streams[i].data_channel_id =
            (uint16_t)(AC_TRANSPORT_AUDIO_DATA_CHANNEL_FIRST + i);
        audio->streams[i].driver = driver;
    }
    return 0;
}

void ac_audio_deinit(ac_audio_controller_t* audio)
{
    size_t i;

    if (!audio)
        return;
    for (i = 0u; i < AC_TRANSPORT_AUDIO_MAX_STREAMS; ++i) {
        if (audio->streams[i].allocated)
            (void)ac_audio_close(audio, audio->streams[i].id);
    }
}

int ac_audio_listen(ac_audio_controller_t* audio,
                    struct ac_transport_controller* transport)
{
    if (!audio || !transport)
        return -1;
    if (ac_transport_register_channel(transport,
                                      AC_TRANSPORT_CHANNEL_AUDIO_CONTROL,
                                      "audio-control",
                                      ac_audio_control_transport_handler,
                                      audio) != 0)
        return -1;
    return ac_transport_open_channel(transport,
                                     AC_TRANSPORT_CHANNEL_AUDIO_CONTROL);
}

int ac_audio_open(ac_audio_controller_t* audio,
                  uint32_t direction,
                  const char* stream_name,
                  uint32_t* stream_id)
{
    size_t i;

    if (stream_id)
        *stream_id = 0u;
    if (!audio || !stream_id || !ac_audio_valid_direction(direction) ||
        !stream_name || stream_name[0] == '\0')
        return -1;

    for (i = 0u; i < AC_TRANSPORT_AUDIO_MAX_STREAMS; ++i) {
        if (!audio->streams[i].allocated) {
            ac_audio_reset_slot(&audio->streams[i]);
            audio->streams[i].allocated = 1;
            audio->streams[i].direction = direction;
            ac_audio_copy_name(audio->streams[i].stream_name,
                               sizeof(audio->streams[i].stream_name),
                               stream_name, strlen(stream_name));
            *stream_id = audio->streams[i].id;
            return 0;
        }
    }
    return -1;
}

int ac_audio_configure(ac_audio_controller_t* audio,
                       uint32_t stream_id,
                       const ac_audio_stream_config_t* config)
{
    ac_audio_stream_slot_t* slot;

    slot = ac_audio_find_stream(audio, stream_id);
    if (!slot || !config)
        return -1;
    if (slot->running)
        return -1;
    slot->config = *config;
    return ac_audio_prepare_sof_stream(slot);
}

int ac_audio_start(ac_audio_controller_t* audio,
                   struct ac_transport_controller* transport,
                   uint32_t stream_id,
                   uint16_t* data_channel_id)
{
    ac_audio_stream_slot_t* slot;

    if (data_channel_id)
        *data_channel_id = 0u;
    slot = ac_audio_find_stream(audio, stream_id);
    if (!slot || !transport || !data_channel_id || !slot->configured ||
        !slot->stream)
        return -1;
    if (!slot->channel_registered) {
        if (ac_transport_register_channel(transport, slot->data_channel_id,
                                          "audio-data",
                                          ac_audio_data_transport_handler,
                                          audio) != 0)
            return -1;
        slot->channel_registered = 1;
    }
    if (ac_transport_open_channel(transport, slot->data_channel_id) != 0)
        return -1;
    if (!slot->running) {
        if (slot->direction == AC_TRANSPORT_AUDIO_DIRECTION_CAPTURE) {
            if (sof_stream_trigger(slot->stream, STREAM_TRIG_START) != 0)
                return -1;
            slot->stream_started = 1;
        }
        slot->running = 1;
    }
    *data_channel_id = slot->data_channel_id;
    return 0;
}

int ac_audio_stop(ac_audio_controller_t* audio, uint32_t stream_id)
{
    ac_audio_stream_slot_t* slot;

    slot = ac_audio_find_stream(audio, stream_id);
    if (!slot)
        return -1;
    if (slot->running && slot->stream && slot->stream_started) {
        if (sof_stream_trigger(slot->stream, STREAM_TRIG_STOP) != 0)
            return -1;
    }
    slot->running = 0;
    slot->stream_started = 0;
    return 0;
}

int ac_audio_close(ac_audio_controller_t* audio, uint32_t stream_id)
{
    ac_audio_stream_slot_t* slot;

    slot = ac_audio_find_stream(audio, stream_id);
    if (!slot)
        return -1;
    (void)ac_audio_stop(audio, stream_id);
    ac_audio_release_sof_stream(slot);
    ac_audio_free_dma(slot);
    slot->allocated = 0;
    slot->running = 0;
    slot->stream_started = 0;
    slot->direction = 0u;
    slot->stream_name[0] = '\0';
    slot->frames_written = 0u;
    slot->frames_read = 0u;
    return 0;
}

static int ac_audio_write_stream(ac_audio_stream_slot_t* slot,
                                 ac_transport_controller_t* transport,
                                 const unsigned char* payload,
                                 size_t payload_size)
{
    const unsigned char* current;
    size_t remaining;
    size_t write_size;
    int32_t free_size;
    unsigned int wait_retries;
    int ret;

    if (!slot || !slot->running || !slot->stream ||
        slot->direction != AC_TRANSPORT_AUDIO_DIRECTION_PLAYBACK)
        return -1;
    if (!payload || payload_size == 0u)
        return -1;
    if (slot->frame_bytes == 0u ||
        payload_size % (size_t)slot->frame_bytes != 0u)
        return -1;

    current = payload;
    remaining = payload_size;
    wait_retries = 0u;
    while (remaining > 0u) {
        free_size = sof_stream_get_free_size(slot->stream);
        if (free_size <= 0) {
            if (ac_audio_wait_stream_progress(slot, transport,
                                              &wait_retries) != 0)
                return -1;
            continue;
        }

        write_size = remaining;
        if ((size_t)free_size < write_size)
            write_size = (size_t)free_size;
        write_size = (write_size / (size_t)slot->frame_bytes) *
                     (size_t)slot->frame_bytes;
        if (write_size == 0u) {
            if (ac_audio_wait_stream_progress(slot, transport,
                                              &wait_retries) != 0)
                return -1;
            continue;
        }

        ret = sof_stream_write(slot->stream, (void*)current,
                               (uint32_t)write_size);
        if (ret < 0 || (size_t)ret > write_size)
            return -1;
        if (ret == 0) {
            if (ac_audio_wait_stream_progress(slot, transport,
                                              &wait_retries) != 0)
                return -1;
            continue;
        }
        if ((size_t)ret % (size_t)slot->frame_bytes != 0u)
            return -1;
        if (!slot->stream_started) {
            if (sof_stream_trigger(slot->stream, STREAM_TRIG_START) != 0)
                return -1;
            slot->stream_started = 1;
        }

        current += ret;
        remaining -= (size_t)ret;
        slot->frames_written += (size_t)ret /
                                (size_t)slot->frame_bytes;
        wait_retries = 0u;
    }
    return 0;
}

static int ac_audio_read_stream(ac_audio_stream_slot_t* slot,
                                ac_transport_controller_t* transport,
                                unsigned int max_bytes,
                                unsigned char* payload,
                                size_t capacity,
                                size_t* actual_size)
{
    unsigned char* current;
    size_t remaining;
    size_t read_size;
    int32_t avail_size;
    unsigned int wait_retries;
    int ret;

    if (actual_size)
        *actual_size = 0u;
    if (!slot || !slot->running || !slot->stream || !payload ||
        !actual_size ||
        slot->direction != AC_TRANSPORT_AUDIO_DIRECTION_CAPTURE)
        return -1;
    if (slot->frame_bytes == 0u)
        return -1;
    if (max_bytes == 0u || max_bytes > capacity)
        max_bytes = (unsigned int)capacity;
    max_bytes = (unsigned int)((max_bytes / slot->frame_bytes) *
                               slot->frame_bytes);
    if (max_bytes == 0u)
        return -1;

    current = payload;
    remaining = (size_t)max_bytes;
    wait_retries = 0u;
    while (remaining > 0u) {
        avail_size = sof_stream_get_avail_size(slot->stream);
        if (avail_size <= 0) {
            if (ac_audio_wait_stream_progress(slot, transport,
                                              &wait_retries) != 0)
                return -1;
            continue;
        }

        read_size = remaining;
        if ((size_t)avail_size < read_size)
            read_size = (size_t)avail_size;
        read_size = (read_size / (size_t)slot->frame_bytes) *
                    (size_t)slot->frame_bytes;
        if (read_size == 0u) {
            if (ac_audio_wait_stream_progress(slot, transport,
                                              &wait_retries) != 0)
                return -1;
            continue;
        }

        ret = sof_stream_read(slot->stream, current, (uint32_t)read_size);
        if (ret < 0 || (size_t)ret > read_size)
            return -1;
        if (ret == 0) {
            if (ac_audio_wait_stream_progress(slot, transport,
                                              &wait_retries) != 0)
                return -1;
            continue;
        }
        if ((size_t)ret % (size_t)slot->frame_bytes != 0u)
            return -1;

        current += ret;
        remaining -= (size_t)ret;
        *actual_size += (size_t)ret;
        slot->frames_read += (size_t)ret / (size_t)slot->frame_bytes;
        wait_retries = 0u;
    }
    return 0;
}

int ac_audio_control_transport_handler(void* user,
                                       struct ac_transport_controller* transport,
                                       const struct ac_transport_frame* request)
{
    ac_audio_controller_t* audio;
    ac_audio_stream_config_t config;
    unsigned char response[AC_AUDIO_START_RESPONSE_SIZE];
    uint32_t stream_id;
    uint32_t direction;
    uint16_t data_channel_id;

    audio = (ac_audio_controller_t*)user;
    if (!audio || !transport || !request)
        return -1;

    if (request->command_id == AC_TRANSPORT_AUDIO_OPEN) {
        if (request->payload_size < AC_AUDIO_OPEN_MIN_REQUEST_SIZE)
            return ac_transport_send_error(transport, request,
                                           "bad audio open request");
        direction = ac_audio_read_le32(request->payload);
        if (ac_audio_open(audio, direction,
                          (const char*)request->payload +
                              AC_AUDIO_OPEN_MIN_REQUEST_SIZE,
                          &stream_id) != 0)
            return ac_transport_send_error(transport, request,
                                           "audio open failed");
        ac_audio_write_le32(response, stream_id);
        return ac_transport_send_response(transport, request, 0u, response,
                                          AC_AUDIO_OPEN_RESPONSE_SIZE);
    }

    if (request->payload_size < AC_AUDIO_STREAM_ID_REQUEST_SIZE)
        return ac_transport_send_error(transport, request,
                                       "bad audio stream request");
    stream_id = ac_audio_read_le32(request->payload);

    if (request->command_id == AC_TRANSPORT_AUDIO_CONFIG) {
        int ret;

        if (request->payload_size < AC_AUDIO_CONFIG_REQUEST_SIZE)
            return ac_transport_send_error(transport, request,
                                           "bad audio config request");
        config.sample_rate = ac_audio_read_le32(request->payload + 4u);
        config.channels = ac_audio_read_le16(request->payload + 8u);
        config.bytes_per_sample = ac_audio_read_le16(request->payload + 10u);
        ret = ac_audio_configure(audio, stream_id, &config);
        if (ret != 0)
            return ac_transport_send_error(transport, request,
                                           ac_audio_config_error_message(ret));
        return ac_transport_send_response(transport, request, 0u, 0, 0u);
    }

    if (request->command_id == AC_TRANSPORT_AUDIO_START) {
        if (ac_audio_start(audio, transport, stream_id,
                           &data_channel_id) != 0)
            return ac_transport_send_error(transport, request,
                                           "audio start failed");
        ac_audio_write_le32(response, stream_id);
        ac_audio_write_le16(response + 4u, data_channel_id);
        ac_audio_write_le16(response + 6u, 0u);
        return ac_transport_send_response(transport, request, 0u, response,
                                          AC_AUDIO_START_RESPONSE_SIZE);
    }

    if (request->command_id == AC_TRANSPORT_AUDIO_STOP) {
        ac_audio_stream_slot_t* slot = ac_audio_find_stream(audio,
                                                            stream_id);

        if (ac_audio_stop(audio, stream_id) != 0)
            return ac_transport_send_error(transport, request,
                                           "audio stop failed");
        if (slot && slot->channel_registered)
            ac_transport_close_channel(transport, slot->data_channel_id);
        return ac_transport_send_response(transport, request, 0u, 0, 0u);
    }

    if (request->command_id == AC_TRANSPORT_AUDIO_CLOSE) {
        ac_audio_stream_slot_t* slot = ac_audio_find_stream(audio,
                                                            stream_id);

        if (slot && slot->channel_registered)
            ac_transport_close_channel(transport, slot->data_channel_id);
        if (ac_audio_close(audio, stream_id) != 0)
            return ac_transport_send_error(transport, request,
                                           "audio close failed");
        return ac_transport_send_response(transport, request, 0u, 0, 0u);
    }

    return ac_transport_send_error(transport, request,
                                   "bad audio control command");
}

int ac_audio_data_transport_handler(void* user,
                                    struct ac_transport_controller* transport,
                                    const struct ac_transport_frame* request)
{
    ac_audio_controller_t* audio;
    ac_audio_stream_slot_t* slot;
    unsigned char payload[AC_TRANSPORT_MAX_PAYLOAD];
    size_t payload_size;
    uint32_t max_bytes;

    audio = (ac_audio_controller_t*)user;
    if (!audio || !transport || !request)
        return -1;
    slot = ac_audio_find_stream_by_channel(audio, request->channel_id);
    if (!slot)
        return ac_transport_send_error(transport, request,
                                       "audio stream channel not found");

    if (request->command_id == AC_TRANSPORT_AUDIO_WRITE) {
        if (ac_audio_write_stream(slot, transport, request->payload,
                                  request->payload_size) != 0)
            return ac_transport_send_error(transport, request,
                                           "audio write failed");
        return ac_transport_send_response(transport, request, 0u, 0, 0u);
    }

    if (request->command_id == AC_TRANSPORT_AUDIO_READ) {
        if (request->payload_size >= AC_AUDIO_READ_REQUEST_SIZE)
            max_bytes = ac_audio_read_le32(request->payload);
        else
            max_bytes = AC_TRANSPORT_MAX_PAYLOAD;
        if (ac_audio_read_stream(slot, transport, max_bytes, payload,
                                 sizeof(payload), &payload_size) != 0)
            return ac_transport_send_error(transport, request,
                                           "audio read failed");
        return ac_transport_send_response(transport, request, 0u, payload,
                                          payload_size);
    }

    if (request->command_id == AC_TRANSPORT_AUDIO_DRAIN) {
        if (ac_audio_drain_stream(slot, transport) != 0)
            return ac_transport_send_error(transport, request,
                                           "audio drain failed");
        return ac_transport_send_response(transport, request, 0u, 0, 0u);
    }

    return ac_transport_send_error(transport, request,
                                   "bad audio data command");
}
