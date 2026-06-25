#include "audio_controller.h"
#include "ac_audio.h"
#include "ac_datalink.h"
#include "ac_log.h"
#include "ac_transport.h"
#include "ac_transport_channel.h"
#include "sof-stream.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct test_driver_state {
    int datalink_opened;
    int datalink_closed;
    int log_opened;
    int log_closed;
    int log_started;
    int log_stopped;
    const unsigned char* log_data;
    size_t log_size;
    unsigned int writes;
    const unsigned char* datalink_reads[4];
    size_t datalink_read_sizes[4];
    unsigned int datalink_read_count;
    unsigned int datalink_read_index;
    unsigned int datalink_delay_index;
    unsigned int datalink_delay_min_timeout_ms;
    unsigned int alloc_count;
    unsigned int free_count;
    size_t last_alloc_size;
    size_t last_alloc_alignment;
};

static struct sof_stream test_streams[AC_TRANSPORT_AUDIO_MAX_STREAMS + 1u];
static unsigned int test_stream_open_count;
static unsigned int test_stream_config_count;
static unsigned int test_stream_trigger_count;
static unsigned int test_stream_close_count;
static unsigned int test_stream_write_count;
static unsigned int test_stream_read_count;
static size_t test_stream_written_bytes;
static size_t test_stream_read_bytes;
static uint32_t test_stream_write_max_bytes;
static uint32_t test_stream_read_max_bytes;
static uint32_t test_last_host_period_bytes;
static uint32_t test_last_dma_buffer_size;
static uint32_t test_last_dma_pages;
static void* test_last_dma_addr;

#define TEST_DATALINK_HEADER_SIZE 36u
#define TEST_DATALINK_FLAG_DATA 0x0001u
#define TEST_DATALINK_FLAG_END 0x0008u

struct sof_stream* sof_stream_open(const char* name)
{
    struct sof_stream* stream;

    if (test_stream_open_count >= AC_TRANSPORT_AUDIO_MAX_STREAMS + 1u)
        return 0;
    stream = &test_streams[test_stream_open_count++];
    memset(stream, 0, sizeof(*stream));
    if (name)
        strncpy(stream->name, name, sizeof(stream->name) - 1u);
    return stream;
}

int sof_stream_config(struct sof_stream* stream, struct sof_stream_params* params)
{
    if (!stream || !params)
        return -1;
    stream->free_size = (int32_t)params->dma_buffer.size;
    stream->avail_size = (int32_t)params->dma_buffer.size;
    test_last_host_period_bytes = params->host_period_bytes;
    test_last_dma_buffer_size = params->dma_buffer.size;
    test_last_dma_pages = params->dma_buffer.pages;
    test_last_dma_addr = params->dma_buffer.addr;
    test_stream_config_count++;
    return 0;
}

int sof_stream_trigger(struct sof_stream* stream, enum sof_stream_trigger cmd)
{
    (void)cmd;
    if (!stream)
        return -1;
    test_stream_trigger_count++;
    return 0;
}

int sof_stream_write(struct sof_stream* stream, void* data, uint32_t size)
{
    uint32_t write_size;

    (void)data;
    if (!stream)
        return -1;
    write_size = size;
    if (test_stream_write_max_bytes != 0u &&
        write_size > test_stream_write_max_bytes)
        write_size = test_stream_write_max_bytes;
    test_stream_write_count++;
    test_stream_written_bytes += write_size;
    return (int)write_size;
}

int sof_stream_read(struct sof_stream* stream, void* data, uint32_t size)
{
    uint32_t read_size;

    if (!stream)
        return -1;
    read_size = size;
    if (test_stream_read_max_bytes != 0u &&
        read_size > test_stream_read_max_bytes)
        read_size = test_stream_read_max_bytes;
    memset(data, 0x5a, read_size);
    test_stream_read_count++;
    test_stream_read_bytes += read_size;
    if (stream->avail_size >= (int32_t)read_size)
        stream->avail_size -= (int32_t)read_size;
    return (int)read_size;
}

int sof_stream_poll_position(struct sof_stream* stream)
{
    return stream ? 0 : -1;
}

int sof_stream_close(struct sof_stream* stream)
{
    if (!stream)
        return -1;
    test_stream_close_count++;
    return 0;
}

static void test_write_le16(unsigned char* data, uint16_t value)
{
    data[0] = (unsigned char)(value & 0xffu);
    data[1] = (unsigned char)((value >> 8) & 0xffu);
}

static void test_write_le32(unsigned char* data, uint32_t value)
{
    data[0] = (unsigned char)(value & 0xffu);
    data[1] = (unsigned char)((value >> 8) & 0xffu);
    data[2] = (unsigned char)((value >> 16) & 0xffu);
    data[3] = (unsigned char)((value >> 24) & 0xffu);
}

static uint32_t test_crc32(const unsigned char* data, size_t size)
{
    uint32_t crc;
    size_t i;
    unsigned int bit;

    crc = 0xffffffffu;
    for (i = 0u; i < size; ++i) {
        crc ^= data[i];
        for (bit = 0u; bit < 8u; ++bit) {
            if ((crc & 1u) != 0u)
                crc = (crc >> 1) ^ 0xedb88320u;
            else
                crc >>= 1;
        }
    }
    return ~crc;
}

static size_t test_build_datalink_frame(unsigned char* out,
                                        size_t capacity,
                                        uint16_t flags,
                                        uint32_t link_sequence,
                                        uint32_t transport_size,
                                        uint32_t fragment_offset,
                                        uint16_t fragment_index,
                                        uint16_t fragment_count,
                                        const unsigned char* payload,
                                        size_t payload_size)
{
    uint32_t crc;

    assert(out);
    assert(capacity >= TEST_DATALINK_HEADER_SIZE + payload_size);
    memset(out, 0, TEST_DATALINK_HEADER_SIZE);
    out[0] = 'A';
    out[1] = 'S';
    out[2] = 'D';
    out[3] = 'L';
    out[4] = 1u;
    out[5] = TEST_DATALINK_HEADER_SIZE;
    test_write_le16(out + 6u, flags);
    test_write_le32(out + 8u, link_sequence);
    test_write_le32(out + 12u, transport_size);
    test_write_le32(out + 16u, fragment_offset);
    test_write_le32(out + 20u, (uint32_t)payload_size);
    test_write_le16(out + 24u, fragment_index);
    test_write_le16(out + 26u, fragment_count);
    crc = test_crc32(payload, payload_size);
    test_write_le32(out + 28u, crc);
    test_write_le32(out + 32u, 0u);
    crc = test_crc32(out, TEST_DATALINK_HEADER_SIZE);
    test_write_le32(out + 32u, crc);
    if (payload_size > 0u)
        memcpy(out + TEST_DATALINK_HEADER_SIZE, payload, payload_size);
    return TEST_DATALINK_HEADER_SIZE + payload_size;
}

static void* test_alloc(void* user, size_t size, size_t alignment)
{
    struct test_driver_state* state;
    void* ptr;
    size_t actual_size;

    state = (struct test_driver_state*)user;
    if (state) {
        state->alloc_count++;
        state->last_alloc_size = size;
        state->last_alloc_alignment = alignment;
    }

    ptr = 0;
    actual_size = size == 0u ? 1u : size;
    if (alignment > sizeof(void*)) {
        if (posix_memalign(&ptr, alignment, actual_size) != 0)
            return 0;
        memset(ptr, 0, actual_size);
        return ptr;
    }
    return calloc(1u, actual_size);
}

static void test_free(void* user, void* ptr)
{
    struct test_driver_state* state;

    state = (struct test_driver_state*)user;
    if (state)
        state->free_count++;
    free(ptr);
}

static void test_log(void* user, audio_controller_log_level_t level,
                     const char* message)
{
    (void)user;
    (void)level;
    (void)message;
}

static int test_datalink_open(void* user)
{
    struct test_driver_state* state;

    state = (struct test_driver_state*)user;
    state->datalink_opened++;
    return 0;
}

static void test_datalink_close(void* user)
{
    struct test_driver_state* state;

    state = (struct test_driver_state*)user;
    state->datalink_closed++;
}

static int test_datalink_read(void* user, void* buffer, size_t capacity,
                              size_t* actual_size, unsigned int timeout_ms)
{
    struct test_driver_state* state;
    unsigned int index;
    size_t size;

    state = (struct test_driver_state*)user;
    *actual_size = 0u;
    if (!state || state->datalink_read_index >= state->datalink_read_count)
        return 1;
    index = state->datalink_read_index;
    if (index == state->datalink_delay_index &&
        timeout_ms < state->datalink_delay_min_timeout_ms)
        return 1;
    size = state->datalink_read_sizes[index];
    if (size > capacity)
        return -1;
    memcpy(buffer, state->datalink_reads[index], size);
    *actual_size = size;
    state->datalink_read_index++;
    return 0;
}

static void test_datalink_queue_read(struct test_driver_state* state,
                                     unsigned int index,
                                     const unsigned char* data,
                                     size_t size)
{
    assert(index < sizeof(state->datalink_reads) /
                   sizeof(state->datalink_reads[0]));
    state->datalink_reads[index] = data;
    state->datalink_read_sizes[index] = size;
    if (state->datalink_read_count <= index)
        state->datalink_read_count = index + 1u;
}

static void test_datalink_clear_reads(struct test_driver_state* state)
{
    memset(state->datalink_reads, 0, sizeof(state->datalink_reads));
    memset(state->datalink_read_sizes, 0, sizeof(state->datalink_read_sizes));
    state->datalink_read_count = 0u;
    state->datalink_read_index = 0u;
    state->datalink_delay_index = 0xffffffffu;
    state->datalink_delay_min_timeout_ms = 0u;
}

static int test_datalink_read_empty(void* user, void* buffer,
                                    size_t capacity, size_t* actual_size,
                                    unsigned int timeout_ms)
{
    (void)user;
    (void)buffer;
    (void)capacity;
    (void)timeout_ms;
    *actual_size = 0u;
    return 1;
}

static int test_datalink_write(void* user, const void* data, size_t size,
                               unsigned int timeout_ms)
{
    struct test_driver_state* state;

    (void)data;
    (void)size;
    (void)timeout_ms;
    state = (struct test_driver_state*)user;
    state->writes++;
    return 0;
}

static size_t test_datalink_mtu(void* user)
{
    (void)user;
    return 37u;
}

static size_t test_datalink_mtu_512(void* user)
{
    (void)user;
    return 512u;
}

static int test_log_source_open(void* user)
{
    struct test_driver_state* state;

    state = (struct test_driver_state*)user;
    state->log_opened++;
    return 0;
}

static void test_log_source_close(void* user)
{
    struct test_driver_state* state;

    state = (struct test_driver_state*)user;
    state->log_closed++;
}

static int test_log_source_start(void* user)
{
    struct test_driver_state* state;

    state = (struct test_driver_state*)user;
    state->log_started++;
    return 0;
}

static int test_log_source_read(void* user, void* buffer, size_t capacity,
                                size_t* actual_size,
                                unsigned int timeout_ms)
{
    struct test_driver_state* state;
    size_t size;

    (void)timeout_ms;
    state = (struct test_driver_state*)user;
    *actual_size = 0u;
    if (state->log_size == 0u)
        return 1;
    size = state->log_size < capacity ? state->log_size : capacity;
    memcpy(buffer, state->log_data, size);
    *actual_size = size;
    state->log_data += size;
    state->log_size -= size;
    return 0;
}

static void test_log_source_stop(void* user)
{
    struct test_driver_state* state;

    state = (struct test_driver_state*)user;
    state->log_stopped++;
}

static void test_log_controller_ops(audio_controller_log_source_ops_t* ops,
                                    struct test_driver_state* state)
{
    ac_log_controller_t log;
    unsigned char buffer[16];
    size_t actual_size;
    static const unsigned char raw[] = { 0x11u, 0x22u, 0x33u, 0x44u };

    memset(&log, 0, sizeof(log));
    assert(ac_log_init(&log, ops) == 0);
    assert(state->log_opened == 0);
    assert(ac_log_read(&log, buffer, sizeof(buffer), &actual_size, 0u) == 1);
    assert(actual_size == 0u);

    assert(ac_log_start(&log) == 0);
    assert(state->log_opened == 1);
    assert(state->log_started == 1);
    assert(ac_log_read(&log, buffer, sizeof(buffer), &actual_size, 0u) == 1);
    assert(actual_size == 0u);

    state->log_data = raw;
    state->log_size = sizeof(raw);
    assert(ac_log_read(&log, buffer, sizeof(buffer), &actual_size, 0u) == 0);
    assert(actual_size == sizeof(raw));
    assert(memcmp(buffer, raw, sizeof(raw)) == 0);

    ac_log_stop(&log);
    assert(state->log_stopped == 1);
    ac_log_deinit(&log);
    assert(state->log_closed == 1);
}

static void test_audio_stream_pool_allows_sixteen_streams(
    audio_controller_driver_ops_t* driver)
{
    ac_audio_controller_t audio;
    uint32_t stream_ids[AC_TRANSPORT_AUDIO_MAX_STREAMS];
    uint32_t stream_id;
    unsigned int i;

    assert(AC_TRANSPORT_AUDIO_MAX_STREAMS == 16u);
    assert(AC_TRANSPORT_AUDIO_DATA_CHANNEL_FIRST == 4u);
    assert(AC_TRANSPORT_AUDIO_DATA_CHANNEL_LAST == 19u);
    assert(AC_TRANSPORT_MAX_CHANNELS >=
           AC_TRANSPORT_AUDIO_DATA_CHANNEL_LAST + 1u);

    memset(&audio, 0, sizeof(audio));
    assert(ac_audio_init(&audio, driver) == 0);
    for (i = 0u; i < AC_TRANSPORT_AUDIO_MAX_STREAMS; ++i) {
        assert(ac_audio_open(&audio, AC_TRANSPORT_AUDIO_DIRECTION_PLAYBACK,
                             "pcm-playback", &stream_ids[i]) == 0);
        assert(stream_ids[i] != 0u);
    }

    assert(ac_audio_open(&audio, AC_TRANSPORT_AUDIO_DIRECTION_PLAYBACK,
                         "too-many", &stream_id) != 0);
    assert(ac_audio_close(&audio, stream_ids[0]) == 0);
    assert(ac_audio_open(&audio, AC_TRANSPORT_AUDIO_DIRECTION_PLAYBACK,
                         "reused", &stream_id) == 0);
    assert(stream_id == stream_ids[0]);

    ac_audio_deinit(&audio);
}

static void test_audio_start_assigns_independent_data_channels(
    audio_controller_driver_ops_t* driver)
{
    ac_audio_controller_t audio;
    ac_transport_controller_t transport;
    uint32_t playback_id;
    uint32_t capture_id;
    uint16_t playback_channel;
    uint16_t capture_channel;
    ac_audio_stream_config_t config;

    memset(&audio, 0, sizeof(audio));
    memset(&transport, 0, sizeof(transport));
    memset(&config, 0, sizeof(config));
    test_stream_open_count = 0u;
    test_stream_config_count = 0u;
    test_stream_trigger_count = 0u;
    test_stream_close_count = 0u;

    assert(ac_transport_init(&transport, driver) == 0);
    assert(ac_audio_init(&audio, driver) == 0);
    assert(ac_audio_open(&audio, AC_TRANSPORT_AUDIO_DIRECTION_PLAYBACK,
                         "pcm-playback", &playback_id) == 0);
    assert(ac_audio_open(&audio, AC_TRANSPORT_AUDIO_DIRECTION_CAPTURE,
                         "pcm-capture", &capture_id) == 0);

    config.sample_rate = 48000u;
    config.channels = 2u;
    config.bytes_per_sample = 2u;
    assert(ac_audio_configure(&audio, playback_id, &config) == 0);
    assert(test_last_host_period_bytes == 768u);
    assert(test_last_dma_buffer_size == 4096u);
    assert(test_last_dma_pages == 1u);
    assert(test_last_dma_addr != 0);
    assert(((uintptr_t)test_last_dma_addr %
            (uintptr_t)AC_AUDIO_DMA_ALIGNMENT) == 0u);
    assert(ac_audio_configure(&audio, capture_id, &config) == 0);
    assert(test_last_host_period_bytes == 768u);
    assert(test_last_dma_buffer_size == 4096u);
    assert(test_last_dma_pages == 1u);
    assert(test_last_dma_addr != 0);
    assert(((uintptr_t)test_last_dma_addr %
            (uintptr_t)AC_AUDIO_DMA_ALIGNMENT) == 0u);
    assert(ac_audio_start(&audio, &transport, playback_id,
                          &playback_channel) == 0);
    assert(ac_audio_start(&audio, &transport, capture_id,
                          &capture_channel) == 0);
    assert(playback_channel == AC_TRANSPORT_AUDIO_DATA_CHANNEL_FIRST);
    assert(capture_channel == AC_TRANSPORT_AUDIO_DATA_CHANNEL_FIRST + 1u);
    assert(playback_channel != capture_channel);
    assert(test_stream_open_count == 2u);
    assert(test_stream_config_count == 2u);

    assert(ac_audio_close(&audio, playback_id) == 0);
    assert(ac_audio_close(&audio, capture_id) == 0);
    assert(test_stream_close_count == 2u);
    ac_audio_deinit(&audio);
    ac_transport_deinit(&transport);
}

static void test_audio_write_uses_blocking_write(
    audio_controller_driver_ops_t* driver)
{
    ac_audio_controller_t audio;
    ac_transport_controller_t transport;
    ac_audio_stream_config_t config;
    ac_transport_frame_t request;
    unsigned char payload[16];
    uint32_t stream_id;
    uint16_t data_channel;

    memset(&audio, 0, sizeof(audio));
    memset(&transport, 0, sizeof(transport));
    memset(&config, 0, sizeof(config));
    memset(&request, 0, sizeof(request));
    memset(payload, 0xa5, sizeof(payload));
    test_stream_open_count = 0u;
    test_stream_config_count = 0u;
    test_stream_trigger_count = 0u;
    test_stream_close_count = 0u;
    test_stream_write_count = 0u;
    test_stream_written_bytes = 0u;
    test_stream_write_max_bytes = 8u;

    assert(ac_transport_init(&transport, driver) == 0);
    assert(ac_audio_init(&audio, driver) == 0);
    assert(ac_audio_open(&audio, AC_TRANSPORT_AUDIO_DIRECTION_PLAYBACK,
                         "pcm-playback", &stream_id) == 0);
    config.sample_rate = 48000u;
    config.channels = 2u;
    config.bytes_per_sample = 2u;
    assert(ac_audio_configure(&audio, stream_id, &config) == 0);
    assert(ac_audio_start(&audio, &transport, stream_id,
                          &data_channel) == 0);

    request.version = 1u;
    request.channel_id = data_channel;
    request.command_id = AC_TRANSPORT_AUDIO_WRITE;
    request.flags = AC_TRANSPORT_FLAG_REQUEST;
    request.sequence_id = 7u;
    request.session_id = stream_id;
    request.payload = payload;
    request.payload_size = sizeof(payload);
    (void)ac_audio_data_transport_handler(&audio, &transport, &request);

    assert(test_stream_write_count == 2u);
    assert(test_stream_written_bytes == sizeof(payload));
    assert(audio.streams[0].frames_written == 4u);

    test_stream_write_max_bytes = 0u;
    ac_audio_deinit(&audio);
    ac_transport_deinit(&transport);
}

static void test_audio_read_uses_blocking_read(
    audio_controller_driver_ops_t* driver)
{
    ac_audio_controller_t audio;
    ac_transport_controller_t transport;
    ac_audio_stream_config_t config;
    ac_transport_frame_t request;
    unsigned char payload[4];
    uint32_t stream_id;
    uint16_t data_channel;

    memset(&audio, 0, sizeof(audio));
    memset(&transport, 0, sizeof(transport));
    memset(&config, 0, sizeof(config));
    memset(&request, 0, sizeof(request));
    test_stream_open_count = 0u;
    test_stream_config_count = 0u;
    test_stream_trigger_count = 0u;
    test_stream_close_count = 0u;
    test_stream_read_count = 0u;
    test_stream_read_bytes = 0u;
    test_stream_read_max_bytes = 8u;

    assert(ac_transport_init(&transport, driver) == 0);
    assert(ac_audio_init(&audio, driver) == 0);
    assert(ac_audio_open(&audio, AC_TRANSPORT_AUDIO_DIRECTION_CAPTURE,
                         "stream_0", &stream_id) == 0);
    config.sample_rate = 48000u;
    config.channels = 2u;
    config.bytes_per_sample = 2u;
    assert(ac_audio_configure(&audio, stream_id, &config) == 0);
    assert(ac_audio_start(&audio, &transport, stream_id,
                          &data_channel) == 0);

    test_write_le32(payload, 16u);
    request.version = 1u;
    request.channel_id = data_channel;
    request.command_id = AC_TRANSPORT_AUDIO_READ;
    request.flags = AC_TRANSPORT_FLAG_REQUEST;
    request.sequence_id = 8u;
    request.session_id = stream_id;
    request.payload = payload;
    request.payload_size = sizeof(payload);
    (void)ac_audio_data_transport_handler(&audio, &transport, &request);

    assert(test_stream_read_count == 2u);
    assert(test_stream_read_bytes == 16u);
    assert(audio.streams[0].frames_read == 4u);

    test_stream_read_max_bytes = 0u;
    ac_audio_deinit(&audio);
    ac_transport_deinit(&transport);
}

static void test_datalink_receive_waits_for_remaining_fragments(
    struct test_driver_state* state,
    audio_controller_datalink_device_ops_t* ops)
{
    ac_datalink_controller_t datalink;
    audio_controller_datalink_device_ops_t local_ops;
    unsigned char payload[600];
    unsigned char received[sizeof(payload)];
    unsigned char frame0[512];
    unsigned char frame1[256];
    size_t frame0_size;
    size_t frame1_size;
    size_t actual_size;
    size_t i;

    for (i = 0u; i < sizeof(payload); ++i)
        payload[i] = (unsigned char)(i & 0xffu);

    frame0_size = test_build_datalink_frame(
        frame0, sizeof(frame0), TEST_DATALINK_FLAG_DATA, 42u,
        (uint32_t)sizeof(payload), 0u, 0u, 2u, payload, 476u);
    frame1_size = test_build_datalink_frame(
        frame1, sizeof(frame1),
        (uint16_t)(TEST_DATALINK_FLAG_DATA | TEST_DATALINK_FLAG_END),
        42u, (uint32_t)sizeof(payload), 476u, 1u, 2u,
        payload + 476u, sizeof(payload) - 476u);

    test_datalink_clear_reads(state);
    test_datalink_queue_read(state, 0u, frame0, frame0_size);
    test_datalink_queue_read(state, 1u, frame1, frame1_size);
    state->datalink_delay_index = 1u;
    state->datalink_delay_min_timeout_ms = 100u;
    state->writes = 0u;

    local_ops = *ops;
    local_ops.mtu = test_datalink_mtu_512;
    memset(&datalink, 0, sizeof(datalink));
    assert(ac_datalink_init(&datalink, &local_ops) == 0);
    memset(received, 0, sizeof(received));
    actual_size = 0u;
    assert(ac_datalink_receive_packet(&datalink, received,
                                      sizeof(received), &actual_size,
                                      1u) == 0);
    assert(actual_size == sizeof(payload));
    assert(memcmp(received, payload, sizeof(payload)) == 0);
    assert(state->writes == 2u);
    ac_datalink_deinit(&datalink);
    test_datalink_clear_reads(state);
}

int main(void)
{
    struct test_driver_state state;
    audio_controller_datalink_device_ops_t datalink;
    audio_controller_log_source_ops_t log_source;
    audio_controller_driver_ops_t driver;
    audio_controller_create_params_t params;
    audio_controller_transport_stats_t stats;
    audio_controller_t* controller;

    memset(&state, 0, sizeof(state));
    memset(&datalink, 0, sizeof(datalink));
    datalink.user = &state;
    datalink.open = test_datalink_open;
    datalink.close = test_datalink_close;
    datalink.read = test_datalink_read;
    datalink.write = test_datalink_write;
    datalink.mtu = test_datalink_mtu;

    memset(&log_source, 0, sizeof(log_source));
    log_source.user = &state;
    log_source.open = test_log_source_open;
    log_source.close = test_log_source_close;
    log_source.start = test_log_source_start;
    log_source.read = test_log_source_read;
    log_source.stop = test_log_source_stop;

    memset(&driver, 0, sizeof(driver));
    driver.user = &state;
    driver.alloc = test_alloc;
    driver.free = test_free;
    driver.log = test_log;
    driver.datalink = &datalink;
    driver.log_source = &log_source;

    memset(&params, 0, sizeof(params));
    params.driver = &driver;

    controller = audio_controller_create(&params);
    assert(controller);
    assert(audio_controller_get_transport_stats(controller, &stats) == 0);
    assert(stats.initialized);
    assert(stats.datalink_open);
    assert(stats.datalink_mtu == 37u);
    assert(state.datalink_opened == 1);
    assert(state.log_opened == 0);

    audio_controller_destroy(controller);
    assert(state.datalink_closed == 1);
    assert(state.log_closed == 0);

    memset(&state, 0, sizeof(state));
    log_source.user = &state;
    test_log_controller_ops(&log_source, &state);
    assert(state.log_closed == 1);
    test_datalink_receive_waits_for_remaining_fragments(&state, &datalink);
    test_audio_stream_pool_allows_sixteen_streams(&driver);
    test_audio_start_assigns_independent_data_channels(&driver);
    test_audio_write_uses_blocking_write(&driver);
    test_audio_read_uses_blocking_read(&driver);
    return 0;
}
