#include "ac_transport.h"

#include <string.h>

#define AC_TRANSPORT_RESPONSE_TIMEOUT_MS 1000u
#define AC_TRANSPORT_RX_POLL_TIMEOUT_MS 1u
#define AC_TRANSPORT_RX_IDLE_SLEEP_MS 1u

static uint16_t ac_read_le16(const unsigned char* data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t ac_read_le32(const unsigned char* data)
{
    return ((uint32_t)data[0]) |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static void ac_write_le16(unsigned char* data, uint16_t value)
{
    data[0] = (unsigned char)(value & 0xffu);
    data[1] = (unsigned char)((value >> 8) & 0xffu);
}

static void ac_write_le32(unsigned char* data, uint32_t value)
{
    data[0] = (unsigned char)(value & 0xffu);
    data[1] = (unsigned char)((value >> 8) & 0xffu);
    data[2] = (unsigned char)((value >> 16) & 0xffu);
    data[3] = (unsigned char)((value >> 24) & 0xffu);
}

static uint32_t ac_crc32(const unsigned char* data, size_t size)
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

static int ac_transport_is_magic(const unsigned char* data)
{
    return data[0] == 'A' && data[1] == 'S' &&
           data[2] == 'T' && data[3] == 'M';
}

static int ac_transport_decode(const unsigned char* data,
                               size_t size,
                               ac_transport_frame_t* frame)
{
    unsigned char header_copy[AC_TRANSPORT_HEADER_SIZE];
    uint32_t payload_size;
    uint32_t expected_header_crc;
    uint32_t expected_payload_crc;

    if (!data || !frame || size < AC_TRANSPORT_HEADER_SIZE)
        return -1;
    if (!ac_transport_is_magic(data))
        return -1;
    if (ac_read_le16(data + 6u) != AC_TRANSPORT_HEADER_SIZE)
        return -1;

    payload_size = ac_read_le32(data + 24u);
    if (size != AC_TRANSPORT_HEADER_SIZE + (size_t)payload_size)
        return -1;
    memcpy(header_copy, data, AC_TRANSPORT_HEADER_SIZE);
    expected_header_crc = ac_read_le32(header_copy + 32u);
    ac_write_le32(header_copy + 32u, 0u);
    if (ac_crc32(header_copy, AC_TRANSPORT_HEADER_SIZE) !=
        expected_header_crc)
        return -1;
    expected_payload_crc = ac_read_le32(data + 28u);
    if (ac_crc32(data + AC_TRANSPORT_HEADER_SIZE, payload_size) !=
        expected_payload_crc)
        return -1;

    memset(frame, 0, sizeof(*frame));
    frame->version = ac_read_le16(data + 4u);
    frame->channel_id = ac_read_le16(data + 8u);
    frame->command_id = ac_read_le16(data + 10u);
    frame->flags = ac_read_le32(data + 12u);
    frame->sequence_id = ac_read_le32(data + 16u);
    frame->session_id = ac_read_le32(data + 20u);
    frame->payload = data + AC_TRANSPORT_HEADER_SIZE;
    frame->payload_size = (size_t)payload_size;
    return 0;
}

static int ac_transport_encode(unsigned char* out,
                               size_t capacity,
                               const ac_transport_frame_t* request,
                               uint32_t flags,
                               const unsigned char* payload,
                               size_t payload_size,
                               size_t* actual_size)
{
    uint32_t header_crc;
    uint32_t payload_crc;

    if (!out || !request || !actual_size)
        return -1;
    if (payload_size > AC_TRANSPORT_MAX_PAYLOAD ||
        capacity < AC_TRANSPORT_HEADER_SIZE + payload_size)
        return -1;
    if (!payload && payload_size > 0u)
        return -1;

    memset(out, 0, AC_TRANSPORT_HEADER_SIZE);
    out[0] = 'A';
    out[1] = 'S';
    out[2] = 'T';
    out[3] = 'M';
    ac_write_le16(out + 4u, request->version == 0u ? 1u : request->version);
    ac_write_le16(out + 6u, AC_TRANSPORT_HEADER_SIZE);
    ac_write_le16(out + 8u, request->channel_id);
    ac_write_le16(out + 10u, request->command_id);
    ac_write_le32(out + 12u, flags);
    ac_write_le32(out + 16u, request->sequence_id);
    ac_write_le32(out + 20u, request->session_id);
    ac_write_le32(out + 24u, (uint32_t)payload_size);
    payload_crc = ac_crc32(payload, payload_size);
    ac_write_le32(out + 28u, payload_crc);
    ac_write_le32(out + 32u, 0u);
    header_crc = ac_crc32(out, AC_TRANSPORT_HEADER_SIZE);
    ac_write_le32(out + 32u, header_crc);
    if (payload_size > 0u)
        memcpy(out + AC_TRANSPORT_HEADER_SIZE, payload, payload_size);
    *actual_size = AC_TRANSPORT_HEADER_SIZE + payload_size;
    return 0;
}

static void ac_transport_sleep(ac_transport_controller_t* transport,
                               unsigned int milliseconds)
{
    if (transport && transport->driver && transport->driver->sleep_ms)
        transport->driver->sleep_ms(transport->driver->user, milliseconds);
}

static void ac_transport_lock_io(ac_transport_controller_t* transport)
{
    if (transport && transport->io_mutex_created && transport->driver &&
        transport->driver->mutex_lock)
        (void)transport->driver->mutex_lock(transport->driver->user,
                                            transport->io_mutex);
}

static void ac_transport_unlock_io(ac_transport_controller_t* transport)
{
    if (transport && transport->io_mutex_created && transport->driver &&
        transport->driver->mutex_unlock)
        (void)transport->driver->mutex_unlock(transport->driver->user,
                                              transport->io_mutex);
}

static void ac_channel_lock(ac_transport_controller_t* transport,
                            ac_transport_channel_runtime_t* channel)
{
    if (transport && channel && channel->mutex_created && transport->driver &&
        transport->driver->mutex_lock)
        (void)transport->driver->mutex_lock(transport->driver->user,
                                            channel->mutex);
}

static void ac_channel_unlock(ac_transport_controller_t* transport,
                              ac_transport_channel_runtime_t* channel)
{
    if (transport && channel && channel->mutex_created && transport->driver &&
        transport->driver->mutex_unlock)
        (void)transport->driver->mutex_unlock(transport->driver->user,
                                              channel->mutex);
}

static ac_transport_channel_runtime_t*
ac_transport_find_channel(ac_transport_controller_t* transport,
                          uint16_t channel_id)
{
    size_t i;

    if (!transport)
        return 0;
    for (i = 0u; i < transport->channel_count; ++i) {
        if (transport->channels[i].id == channel_id)
            return &transport->channels[i];
    }
    return 0;
}

static int ac_transport_pop_request(ac_transport_controller_t* transport,
                                    ac_transport_channel_runtime_t* channel,
                                    ac_transport_queued_request_t* request)
{
    if (!transport || !channel || !request)
        return 0;

    ac_channel_lock(transport, channel);
    if (channel->count == 0u) {
        ac_channel_unlock(transport, channel);
        return 0;
    }
    *request = channel->queue[channel->head];
    channel->head = (channel->head + 1u) % AC_TRANSPORT_CHANNEL_QUEUE_DEPTH;
    channel->count--;
    ac_channel_unlock(transport, channel);

    request->frame.payload = request->payload;
    return 1;
}

static int ac_transport_queue_request(ac_transport_controller_t* transport,
                                      ac_transport_channel_runtime_t* channel,
                                      const ac_transport_frame_t* frame)
{
    ac_transport_queued_request_t* request;

    if (!transport || !channel || !frame)
        return -1;
    if (frame->payload_size > AC_TRANSPORT_MAX_PAYLOAD)
        return -1;

    ac_channel_lock(transport, channel);
    if (channel->count >= AC_TRANSPORT_CHANNEL_QUEUE_DEPTH) {
        ac_channel_unlock(transport, channel);
        return -1;
    }
    request = &channel->queue[channel->tail];
    memset(request, 0, sizeof(*request));
    request->frame = *frame;
    if (frame->payload_size > 0u)
        memcpy(request->payload, frame->payload, frame->payload_size);
    request->frame.payload = request->payload;
    channel->tail = (channel->tail + 1u) % AC_TRANSPORT_CHANNEL_QUEUE_DEPTH;
    channel->count++;
    ac_channel_unlock(transport, channel);
    return 0;
}

static void* ac_transport_channel_worker(void* arg)
{
    ac_transport_channel_runtime_t* channel;
    ac_transport_controller_t* transport;
    ac_transport_queued_request_t request;

    channel = (ac_transport_channel_runtime_t*)arg;
    if (!channel)
        return 0;
    transport = channel->transport;

    while (!channel->stop_requested || channel->count > 0u) {
        if (ac_transport_pop_request(transport, channel, &request)) {
            if (channel->handler)
                (void)channel->handler(channel->handler_user, transport,
                                       &request.frame);
            continue;
        }
        ac_transport_sleep(transport, 10u);
    }
    return 0;
}

static int ac_transport_dispatch_packet(ac_transport_controller_t* transport,
                                        const unsigned char* packet,
                                        size_t packet_size)
{
    ac_transport_frame_t request;
    ac_transport_channel_runtime_t* channel;

    if (ac_transport_decode(packet, packet_size, &request) != 0)
        return -1;
    if ((request.flags & AC_TRANSPORT_FLAG_REQUEST) == 0u)
        return -1;

    channel = ac_transport_find_channel(transport, request.channel_id);
    if (!channel || !channel->open) {
        return ac_transport_send_error(transport, &request, "bad channel");
    }
    if (channel->thread_started)
        return ac_transport_queue_request(transport, channel, &request);
    if (!channel->handler)
        return ac_transport_send_error(transport, &request,
                                       "channel has no handler");
    return channel->handler(channel->handler_user, transport, &request);
}

static void* ac_transport_worker(void* arg)
{
    ac_transport_controller_t* transport;
    unsigned char packet[AC_DATALINK_MAX_PACKET_SIZE];
    size_t packet_size;
    int ret;

    transport = (ac_transport_controller_t*)arg;
    if (!transport)
        return 0;

    while (!transport->stop_requested) {
        packet_size = 0u;
        ac_transport_lock_io(transport);
        ret = ac_datalink_receive_packet(&transport->datalink, packet,
                                         sizeof(packet), &packet_size,
                                         AC_TRANSPORT_RX_POLL_TIMEOUT_MS);
        ac_transport_unlock_io(transport);
        if (ret == 0)
            (void)ac_transport_dispatch_packet(transport, packet,
                                               packet_size);
        ac_transport_sleep(transport, AC_TRANSPORT_RX_IDLE_SLEEP_MS);
    }
    return 0;
}

int ac_transport_init(ac_transport_controller_t* transport,
                      const audio_controller_driver_ops_t* driver)
{
    if (!transport || !driver)
        return -1;

    memset(transport, 0, sizeof(*transport));
    transport->driver = driver;
    if (driver->mutex_create && driver->mutex_destroy &&
        driver->mutex_lock && driver->mutex_unlock) {
        if (driver->mutex_create(driver->user, &transport->io_mutex) != 0)
            return -1;
        transport->io_mutex_created = 1;
    }
    if (ac_datalink_init(&transport->datalink, driver->datalink) != 0) {
        if (transport->io_mutex_created)
            driver->mutex_destroy(driver->user, transport->io_mutex);
        return -1;
    }

    transport->initialized = 1;
    return 0;
}

int ac_transport_register_channel(ac_transport_controller_t* transport,
                                  uint16_t channel_id,
                                  const char* name,
                                  ac_transport_channel_handler_t handler,
                                  void* handler_user)
{
    ac_transport_channel_runtime_t* channel;

    if (!transport || channel_id == 0u || !handler)
        return -1;
    if (ac_transport_find_channel(transport, channel_id))
        return -1;
    if (transport->channel_count >= AC_TRANSPORT_MAX_CHANNELS)
        return -1;

    channel = &transport->channels[transport->channel_count];
    memset(channel, 0, sizeof(*channel));
    channel->transport = transport;
    channel->id = channel_id;
    channel->name = name;
    channel->handler = handler;
    channel->handler_user = handler_user;
    if (transport->driver && transport->driver->mutex_create &&
        transport->driver->mutex_destroy &&
        transport->driver->mutex_lock &&
        transport->driver->mutex_unlock) {
        if (transport->driver->mutex_create(transport->driver->user,
                                            &channel->mutex) != 0)
            return -1;
        channel->mutex_created = 1;
    }
    transport->channel_count++;
    return 0;
}

int ac_transport_open_channel(ac_transport_controller_t* transport,
                              uint16_t channel_id)
{
    ac_transport_channel_runtime_t* channel;

    channel = ac_transport_find_channel(transport, channel_id);
    if (!transport || !channel)
        return -1;
    channel->open = 1;
    channel->stop_requested = 0;
    if (!channel->thread_started && transport->driver &&
        transport->driver->thread_create && transport->driver->thread_join) {
        if (transport->driver->thread_create(transport->driver->user,
                                             &channel->thread,
                                             ac_transport_channel_worker,
                                             channel) != 0) {
            channel->open = 0;
            return -1;
        }
        channel->thread_started = 1;
    }
    return 0;
}

void ac_transport_close_channel(ac_transport_controller_t* transport,
                                uint16_t channel_id)
{
    ac_transport_channel_runtime_t* channel;

    channel = ac_transport_find_channel(transport, channel_id);
    if (!transport || !channel)
        return;

    channel->open = 0;
    channel->stop_requested = 1;
    if (channel->thread_started && transport->driver &&
        transport->driver->thread_join) {
        (void)transport->driver->thread_join(transport->driver->user,
                                             channel->thread);
    }
    channel->thread_started = 0;
    if (channel->mutex_created && transport->driver &&
        transport->driver->mutex_destroy) {
        transport->driver->mutex_destroy(transport->driver->user,
                                         channel->mutex);
    }
    channel->mutex_created = 0;
    channel->count = 0u;
    channel->head = 0u;
    channel->tail = 0u;
}

int ac_transport_start(ac_transport_controller_t* transport)
{
    if (!transport || !transport->initialized)
        return -1;
    if (transport->worker_started)
        return 0;
    if (transport->driver && transport->driver->datalink &&
        transport->driver->thread_create && transport->driver->thread_join) {
        transport->running = 1;
        transport->stop_requested = 0;
        if (transport->driver->thread_create(transport->driver->user,
                                             &transport->thread,
                                             ac_transport_worker,
                                             transport) != 0) {
            transport->running = 0;
            return -1;
        }
        transport->worker_started = 1;
    }
    return 0;
}

void ac_transport_deinit(ac_transport_controller_t* transport)
{
    size_t i;

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

    for (i = 0u; i < transport->channel_count; ++i)
        ac_transport_close_channel(transport, transport->channels[i].id);
    transport->channel_count = 0u;

    ac_datalink_deinit(&transport->datalink);
    if (transport->io_mutex_created && transport->driver &&
        transport->driver->mutex_destroy) {
        transport->driver->mutex_destroy(transport->driver->user,
                                         transport->io_mutex);
    }
    transport->io_mutex_created = 0;
    transport->initialized = 0;
}

int ac_transport_send_response(ac_transport_controller_t* transport,
                               const ac_transport_frame_t* request,
                               uint32_t extra_flags,
                               const unsigned char* payload,
                               size_t payload_size)
{
    unsigned char packet[AC_DATALINK_MAX_PACKET_SIZE];
    size_t packet_size;
    uint32_t flags;
    int ret;

    flags = AC_TRANSPORT_FLAG_RESPONSE | AC_TRANSPORT_FLAG_ACK | extra_flags;
    if (ac_transport_encode(packet, sizeof(packet), request, flags, payload,
                            payload_size, &packet_size) != 0)
        return -1;
    ac_transport_lock_io(transport);
    ret = ac_datalink_send_packet(&transport->datalink, packet, packet_size,
                                  AC_TRANSPORT_RESPONSE_TIMEOUT_MS);
    ac_transport_unlock_io(transport);
    return ret;
}

int ac_transport_send_error(ac_transport_controller_t* transport,
                            const ac_transport_frame_t* request,
                            const char* message)
{
    const unsigned char* payload;
    size_t size;

    if (!message)
        message = "transport error";
    payload = (const unsigned char*)message;
    size = strlen(message);
    return ac_transport_send_response(transport, request,
                                      AC_TRANSPORT_FLAG_ERROR,
                                      payload, size);
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
