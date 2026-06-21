#include "ac_transport.h"

#include <string.h>

#define AC_TRANSPORT_HEADER_SIZE 36u
#define AC_TRANSPORT_FLAG_REQUEST 0x00000001u
#define AC_TRANSPORT_FLAG_RESPONSE 0x00000002u
#define AC_TRANSPORT_FLAG_ACK 0x00000004u
#define AC_TRANSPORT_FLAG_ERROR 0x00000080u
#define AC_TRANSPORT_CHANNEL_LOG 1u
#define AC_TRANSPORT_LOG_OPEN 1u
#define AC_TRANSPORT_LOG_READ 2u
#define AC_TRANSPORT_RESPONSE_TIMEOUT_MS 1000u
#define AC_TRANSPORT_MAX_PAYLOAD \
    (AC_DATALINK_MAX_PACKET_SIZE - AC_TRANSPORT_HEADER_SIZE)
#define AC_TRANSPORT_LOG_READ_CHUNK 512u

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

static int ac_transport_send_response(ac_transport_controller_t* transport,
                                      const ac_transport_frame_t* request,
                                      uint32_t extra_flags,
                                      const unsigned char* payload,
                                      size_t payload_size)
{
    unsigned char packet[AC_DATALINK_MAX_PACKET_SIZE];
    size_t packet_size;
    uint32_t flags;

    flags = AC_TRANSPORT_FLAG_RESPONSE | AC_TRANSPORT_FLAG_ACK | extra_flags;
    if (ac_transport_encode(packet, sizeof(packet), request, flags, payload,
                            payload_size, &packet_size) != 0)
        return -1;
    return ac_datalink_send_packet(&transport->datalink, packet, packet_size,
                                   AC_TRANSPORT_RESPONSE_TIMEOUT_MS);
}

static int ac_transport_handle_log(ac_transport_controller_t* transport,
                                   const ac_transport_frame_t* request)
{
    unsigned char payload[AC_TRANSPORT_LOG_READ_CHUNK];
    size_t payload_size;

    if (request->command_id == AC_TRANSPORT_LOG_OPEN) {
        if (ac_log_start(&transport->log) != 0)
            return ac_transport_send_response(
                transport, request, AC_TRANSPORT_FLAG_ERROR,
                (const unsigned char*)"log start failed", 16u);
        return ac_transport_send_response(transport, request, 0u, 0, 0u);
    }
    if (request->command_id == AC_TRANSPORT_LOG_READ) {
        if (ac_log_read(&transport->log, payload, sizeof(payload),
                        &payload_size, 100u) < 0)
            return ac_transport_send_response(
                transport, request, AC_TRANSPORT_FLAG_ERROR,
                (const unsigned char*)"log read failed", 15u);
        return ac_transport_send_response(transport, request, 0u, payload,
                                          payload_size);
    }

    return ac_transport_send_response(transport, request,
                                      AC_TRANSPORT_FLAG_ERROR,
                                      (const unsigned char*)"bad log command",
                                      15u);
}

static int ac_transport_handle_packet(ac_transport_controller_t* transport,
                                      const unsigned char* packet,
                                      size_t packet_size)
{
    ac_transport_frame_t request;

    if (ac_transport_decode(packet, packet_size, &request) != 0)
        return -1;
    if ((request.flags & AC_TRANSPORT_FLAG_REQUEST) == 0u)
        return -1;

    if (request.channel_id == AC_TRANSPORT_CHANNEL_LOG)
        return ac_transport_handle_log(transport, &request);

    return ac_transport_send_response(transport, &request,
                                      AC_TRANSPORT_FLAG_ERROR,
                                      (const unsigned char*)"bad channel",
                                      11u);
}

static void* ac_transport_worker(void* arg)
{
    ac_transport_controller_t* transport;
    unsigned char packet[AC_DATALINK_MAX_PACKET_SIZE];
    size_t packet_size;

    transport = (ac_transport_controller_t*)arg;
    if (!transport)
        return 0;

    while (!transport->stop_requested) {
        packet_size = 0u;
        if (ac_datalink_receive_packet(&transport->datalink, packet,
                                       sizeof(packet), &packet_size,
                                       10u) == 0) {
            (void)ac_transport_handle_packet(transport, packet, packet_size);
        }
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
    if (ac_datalink_init(&transport->datalink, driver->datalink) != 0)
        return -1;
    if (ac_log_init(&transport->log, driver->log_source) != 0) {
        ac_datalink_deinit(&transport->datalink);
        return -1;
    }

    transport->initialized = 1;
    if (driver->datalink && driver->thread_create && driver->thread_join) {
        transport->running = 1;
        if (driver->thread_create(driver->user, &transport->thread,
                                  ac_transport_worker, transport) != 0) {
            transport->running = 0;
            ac_log_deinit(&transport->log);
            ac_datalink_deinit(&transport->datalink);
            transport->initialized = 0;
            return -1;
        }
        transport->worker_started = 1;
    }
    return 0;
}

void ac_transport_deinit(ac_transport_controller_t* transport)
{
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
    ac_log_deinit(&transport->log);
    ac_datalink_deinit(&transport->datalink);
    transport->initialized = 0;
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
