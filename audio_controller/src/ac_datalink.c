#include "ac_datalink.h"

#include <string.h>

#define AC_DATALINK_HEADER_SIZE 36u
#define AC_DATALINK_FLAG_DATA 0x0001u
#define AC_DATALINK_FLAG_ACK 0x0002u
#define AC_DATALINK_FLAG_NAK 0x0004u
#define AC_DATALINK_FLAG_END 0x0008u
#define AC_DATALINK_MAX_RETRIES 3u
#define AC_DATALINK_FRAGMENT_TIMEOUT_MS 1000u

typedef struct ac_datalink_frame_view {
    uint16_t flags;
    uint32_t link_sequence;
    uint32_t transport_size;
    uint32_t fragment_offset;
    uint32_t payload_size;
    uint16_t fragment_index;
    uint16_t fragment_count;
    const unsigned char* payload;
} ac_datalink_frame_view_t;

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

static int ac_datalink_is_magic(const unsigned char* data)
{
    return data[0] == 'A' && data[1] == 'S' &&
           data[2] == 'D' && data[3] == 'L';
}

static size_t ac_datalink_frame_size(const unsigned char* data, size_t size)
{
    uint32_t payload_size;

    if (size < AC_DATALINK_HEADER_SIZE)
        return 0u;
    if (!ac_datalink_is_magic(data))
        return 0u;
    if (data[5] != AC_DATALINK_HEADER_SIZE)
        return 0u;
    payload_size = ac_read_le32(data + 20u);
    return AC_DATALINK_HEADER_SIZE + (size_t)payload_size;
}

static int ac_datalink_decode_frame(const unsigned char* frame,
                                    size_t frame_size,
                                    ac_datalink_frame_view_t* view)
{
    uint32_t expected_header_crc;
    uint32_t expected_payload_crc;
    uint32_t header_crc;
    uint32_t payload_crc;
    unsigned char header_copy[AC_DATALINK_HEADER_SIZE];

    if (!frame || !view || frame_size < AC_DATALINK_HEADER_SIZE)
        return -1;
    if (!ac_datalink_is_magic(frame) || frame[5] != AC_DATALINK_HEADER_SIZE)
        return -1;
    if (frame_size != ac_datalink_frame_size(frame, frame_size))
        return -1;

    memcpy(header_copy, frame, AC_DATALINK_HEADER_SIZE);
    expected_header_crc = ac_read_le32(header_copy + 32u);
    ac_write_le32(header_copy + 32u, 0u);
    header_crc = ac_crc32(header_copy, AC_DATALINK_HEADER_SIZE);
    if (header_crc != expected_header_crc)
        return -1;

    expected_payload_crc = ac_read_le32(frame + 28u);
    payload_crc = ac_crc32(frame + AC_DATALINK_HEADER_SIZE,
                           frame_size - AC_DATALINK_HEADER_SIZE);
    if (payload_crc != expected_payload_crc)
        return -1;

    memset(view, 0, sizeof(*view));
    view->flags = ac_read_le16(frame + 6u);
    view->link_sequence = ac_read_le32(frame + 8u);
    view->transport_size = ac_read_le32(frame + 12u);
    view->fragment_offset = ac_read_le32(frame + 16u);
    view->payload_size = ac_read_le32(frame + 20u);
    view->fragment_index = ac_read_le16(frame + 24u);
    view->fragment_count = ac_read_le16(frame + 26u);
    view->payload = frame + AC_DATALINK_HEADER_SIZE;
    return 0;
}

static size_t ac_datalink_effective_mtu(const ac_datalink_controller_t* datalink)
{
    size_t mtu;

    mtu = datalink->mtu;
    if (mtu == 0u)
        mtu = 256u;
    if (mtu > AC_DATALINK_MAX_FRAME_SIZE)
        mtu = AC_DATALINK_MAX_FRAME_SIZE;
    return mtu;
}

static unsigned int ac_datalink_fragment_timeout(unsigned int timeout_ms)
{
    if (timeout_ms < AC_DATALINK_FRAGMENT_TIMEOUT_MS)
        return AC_DATALINK_FRAGMENT_TIMEOUT_MS;
    return timeout_ms;
}

static int ac_datalink_encode_frame(unsigned char* out,
                                    size_t capacity,
                                    uint16_t flags,
                                    uint32_t link_sequence,
                                    uint32_t transport_size,
                                    uint32_t fragment_offset,
                                    uint16_t fragment_index,
                                    uint16_t fragment_count,
                                    const unsigned char* payload,
                                    size_t payload_size,
                                    size_t* actual_size)
{
    uint32_t header_crc;
    uint32_t payload_crc;

    if (!out || !actual_size)
        return -1;
    if (capacity < AC_DATALINK_HEADER_SIZE + payload_size)
        return -1;
    if (!payload && payload_size > 0u)
        return -1;

    memset(out, 0, AC_DATALINK_HEADER_SIZE);
    out[0] = 'A';
    out[1] = 'S';
    out[2] = 'D';
    out[3] = 'L';
    out[4] = 1u;
    out[5] = AC_DATALINK_HEADER_SIZE;
    ac_write_le16(out + 6u, flags);
    ac_write_le32(out + 8u, link_sequence);
    ac_write_le32(out + 12u, transport_size);
    ac_write_le32(out + 16u, fragment_offset);
    ac_write_le32(out + 20u, (uint32_t)payload_size);
    ac_write_le16(out + 24u, fragment_index);
    ac_write_le16(out + 26u, fragment_count);
    payload_crc = ac_crc32(payload, payload_size);
    ac_write_le32(out + 28u, payload_crc);
    ac_write_le32(out + 32u, 0u);
    header_crc = ac_crc32(out, AC_DATALINK_HEADER_SIZE);
    ac_write_le32(out + 32u, header_crc);
    if (payload_size > 0u)
        memcpy(out + AC_DATALINK_HEADER_SIZE, payload, payload_size);
    *actual_size = AC_DATALINK_HEADER_SIZE + payload_size;
    return 0;
}

static int ac_datalink_send_ack(ac_datalink_controller_t* datalink,
                                const ac_datalink_frame_view_t* frame,
                                int ok,
                                unsigned int timeout_ms)
{
    unsigned char ack[AC_DATALINK_HEADER_SIZE];
    size_t actual_size;
    uint16_t flags;

    if (!datalink || !datalink->ops || !datalink->ops->write || !frame)
        return -1;

    flags = ok ? AC_DATALINK_FLAG_ACK : AC_DATALINK_FLAG_NAK;
    if (ac_datalink_encode_frame(ack, sizeof(ack), flags,
                                 frame->link_sequence,
                                 frame->transport_size,
                                 frame->fragment_offset,
                                 frame->fragment_index,
                                 frame->fragment_count,
                                 0, 0u, &actual_size) != 0)
        return -1;

    return datalink->ops->write(datalink->ops->user, ack, actual_size,
                                timeout_ms);
}

static int ac_datalink_read_frame(ac_datalink_controller_t* datalink,
                                  ac_datalink_frame_view_t* view,
                                  unsigned char* frame_copy,
                                  size_t* frame_size,
                                  unsigned int timeout_ms)
{
    unsigned char buffer[AC_DATALINK_MAX_FRAME_SIZE];
    size_t actual;
    size_t complete_size;
    size_t capacity;
    int ret;

    if (!datalink || !view || !frame_copy || !frame_size ||
        !datalink->ops || !datalink->ops->read)
        return -1;

    capacity = ac_datalink_effective_mtu(datalink);
    while (1) {
        while (datalink->rx_stream_size > 0u &&
               !ac_datalink_is_magic(datalink->rx_stream)) {
            memmove(datalink->rx_stream, datalink->rx_stream + 1u,
                    datalink->rx_stream_size - 1u);
            datalink->rx_stream_size--;
        }

        complete_size = ac_datalink_frame_size(datalink->rx_stream,
                                               datalink->rx_stream_size);
        if (complete_size > AC_DATALINK_RX_STREAM_SIZE)
            return -1;
        if (complete_size > 0u &&
            datalink->rx_stream_size >= complete_size) {
            if (ac_datalink_decode_frame(datalink->rx_stream,
                                         complete_size, view) != 0) {
                memmove(datalink->rx_stream, datalink->rx_stream + 1u,
                        datalink->rx_stream_size - 1u);
                datalink->rx_stream_size--;
                continue;
            }
            memcpy(frame_copy, datalink->rx_stream, complete_size);
            *frame_size = complete_size;
            memmove(datalink->rx_stream,
                    datalink->rx_stream + complete_size,
                    datalink->rx_stream_size - complete_size);
            datalink->rx_stream_size -= complete_size;
            view->payload = frame_copy + AC_DATALINK_HEADER_SIZE;
            return 0;
        }

        if (datalink->rx_stream_size + capacity > AC_DATALINK_RX_STREAM_SIZE)
            return -1;

        actual = 0u;
        ret = datalink->ops->read(datalink->ops->user, buffer, capacity,
                                  &actual, timeout_ms);
        if (ret != 0)
            return ret;
        if (actual == 0u)
            return 1;
        memcpy(datalink->rx_stream + datalink->rx_stream_size, buffer, actual);
        datalink->rx_stream_size += actual;
    }
}

int ac_datalink_init(ac_datalink_controller_t* datalink,
                     const audio_controller_datalink_device_ops_t* ops)
{
    if (!datalink)
        return -1;

    memset(datalink, 0, sizeof(*datalink));
    datalink->ops = ops;
    datalink->next_link_sequence = 1u;
    if (!ops)
        return 0;

    if (ops->mtu)
        datalink->mtu = ops->mtu(ops->user);
    if (datalink->mtu == 0u)
        datalink->mtu = 256u;

    if (ops->open) {
        if (ops->open(ops->user) != 0)
            return -1;
        datalink->open = 1;
    }

    return 0;
}

void ac_datalink_deinit(ac_datalink_controller_t* datalink)
{
    if (!datalink)
        return;

    if (datalink->ops && datalink->open && datalink->ops->close)
        datalink->ops->close(datalink->ops->user);

    datalink->open = 0;
}

int ac_datalink_send_packet(ac_datalink_controller_t* datalink,
                            const unsigned char* payload,
                            size_t payload_size,
                            unsigned int timeout_ms)
{
    unsigned char frame[AC_DATALINK_MAX_FRAME_SIZE];
    unsigned char ack_frame[AC_DATALINK_MAX_FRAME_SIZE];
    ac_datalink_frame_view_t ack;
    size_t frame_size;
    size_t ack_frame_size;
    size_t mtu;
    size_t fragment_capacity;
    size_t fragment_count;
    size_t offset;
    size_t count;
    uint32_t link_sequence;
    uint16_t flags;
    unsigned int attempt;
    size_t index;
    int delivered;
    int ret;

    if (!datalink || !datalink->ops || !datalink->open ||
        !datalink->ops->write)
        return -1;
    if (!payload && payload_size > 0u)
        return -1;
    if (payload_size > AC_DATALINK_MAX_PACKET_SIZE)
        return -1;

    mtu = ac_datalink_effective_mtu(datalink);
    if (mtu <= AC_DATALINK_HEADER_SIZE)
        return -1;
    fragment_capacity = mtu - AC_DATALINK_HEADER_SIZE;
    fragment_count = (payload_size + fragment_capacity - 1u) /
                     fragment_capacity;
    if (fragment_count == 0u)
        fragment_count = 1u;
    if (fragment_count > 0xffffu)
        return -1;

    link_sequence = datalink->next_link_sequence++;
    for (index = 0u; index < fragment_count; index++) {
        offset = index * fragment_capacity;
        count = payload_size > offset ? payload_size - offset : 0u;
        if (count > fragment_capacity)
            count = fragment_capacity;

        flags = AC_DATALINK_FLAG_DATA;
        if (index + 1u == fragment_count)
            flags = (uint16_t)(flags | AC_DATALINK_FLAG_END);
        if (ac_datalink_encode_frame(frame, sizeof(frame), flags,
                                     link_sequence,
                                     (uint32_t)payload_size,
                                     (uint32_t)offset,
                                     (uint16_t)index,
                                     (uint16_t)fragment_count,
                                     payload ? payload + offset : 0,
                                     count, &frame_size) != 0)
            return -1;

        delivered = 0;
        for (attempt = 0u; attempt <= AC_DATALINK_MAX_RETRIES; attempt++) {
            ret = datalink->ops->write(datalink->ops->user, frame,
                                       frame_size, timeout_ms);
            if (ret != 0)
                return ret;

            ret = ac_datalink_read_frame(datalink, &ack, ack_frame,
                                         &ack_frame_size, timeout_ms);
            if (ret == 0 &&
                ack.link_sequence == link_sequence &&
                ack.fragment_index == index &&
                (ack.flags & AC_DATALINK_FLAG_ACK) != 0u) {
                delivered = 1;
                break;
            }
            if (ret == 0 && (ack.flags & AC_DATALINK_FLAG_NAK) != 0u)
                datalink->retries++;
            else if (ret != 0)
                datalink->retries++;
        }
        if (!delivered)
            return -1;
    }

    datalink->tx_packets++;
    return 0;
}

int ac_datalink_receive_packet(ac_datalink_controller_t* datalink,
                               unsigned char* payload,
                               size_t capacity,
                               size_t* actual_size,
                               unsigned int timeout_ms)
{
    unsigned char frame_copy[AC_DATALINK_MAX_FRAME_SIZE];
    ac_datalink_frame_view_t first;
    ac_datalink_frame_view_t frame;
    size_t frame_size;
    unsigned int fragment_timeout_ms;
    uint16_t expected;
    int ret;

    if (!actual_size)
        return -1;
    *actual_size = 0u;
    if (!datalink || !datalink->ops || !datalink->open ||
        (!payload && capacity > 0u))
        return -1;

    do {
        ret = ac_datalink_read_frame(datalink, &first, frame_copy,
                                     &frame_size, timeout_ms);
        if (ret != 0)
            return ret;
    } while ((first.flags & AC_DATALINK_FLAG_DATA) == 0u);

    if (first.fragment_offset != 0u || first.fragment_count == 0u)
        return -1;
    if (first.transport_size > capacity ||
        first.transport_size > AC_DATALINK_MAX_PACKET_SIZE)
        return -1;
    if (first.payload_size > first.transport_size)
        return -1;

    if (first.payload_size > 0u)
        memcpy(payload, first.payload, first.payload_size);
    if (ac_datalink_send_ack(datalink, &first, 1, timeout_ms) != 0)
        return -1;

    fragment_timeout_ms = ac_datalink_fragment_timeout(timeout_ms);
    for (expected = 1u; expected < first.fragment_count; expected++) {
        ret = ac_datalink_read_frame(datalink, &frame, frame_copy,
                                     &frame_size, fragment_timeout_ms);
        if (ret != 0)
            return ret;
        if ((frame.flags & AC_DATALINK_FLAG_DATA) == 0u ||
            frame.link_sequence != first.link_sequence ||
            frame.fragment_index != expected ||
            frame.transport_size != first.transport_size ||
            frame.fragment_offset + frame.payload_size > capacity) {
            (void)ac_datalink_send_ack(datalink, &frame, 0, timeout_ms);
            datalink->retries++;
            return -1;
        }
        if (frame.payload_size > 0u) {
            memcpy(payload + frame.fragment_offset, frame.payload,
                   frame.payload_size);
        }
        if (ac_datalink_send_ack(datalink, &frame, 1, timeout_ms) != 0)
            return -1;
    }

    *actual_size = first.transport_size;
    datalink->rx_packets++;
    return 0;
}

int ac_datalink_poll(ac_datalink_controller_t* datalink,
                     unsigned int timeout_ms)
{
    unsigned char payload[AC_DATALINK_MAX_PACKET_SIZE];
    size_t actual;

    return ac_datalink_receive_packet(datalink, payload, sizeof(payload),
                                      &actual, timeout_ms);
}
