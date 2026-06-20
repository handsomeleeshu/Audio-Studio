#include "ac_datalink.h"

#include <string.h>

#define AC_DATALINK_HEADER_SIZE 36u
#define AC_DATALINK_FLAG_DATA 0x0001u
#define AC_DATALINK_FLAG_ACK 0x0002u
#define AC_DATALINK_FLAG_NAK 0x0004u

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

static int ac_datalink_send_ack(ac_datalink_controller_t* datalink,
                                const unsigned char* frame,
                                int ok,
                                unsigned int timeout_ms)
{
    unsigned char ack[AC_DATALINK_HEADER_SIZE];
    uint16_t header_size;
    uint32_t header_crc;

    if (!datalink || !datalink->ops || !datalink->ops->write)
        return -1;

    header_size = frame[5];
    if (header_size != AC_DATALINK_HEADER_SIZE)
        return -1;

    memset(ack, 0, sizeof(ack));
    ack[0] = 'A';
    ack[1] = 'S';
    ack[2] = 'D';
    ack[3] = 'L';
    ack[4] = 1u;
    ack[5] = AC_DATALINK_HEADER_SIZE;
    ac_write_le16(ack + 6u, ok ? AC_DATALINK_FLAG_ACK : AC_DATALINK_FLAG_NAK);
    ac_write_le32(ack + 8u, ac_read_le32(frame + 8u));
    ac_write_le32(ack + 12u, ac_read_le32(frame + 12u));
    ac_write_le32(ack + 16u, ac_read_le32(frame + 16u));
    ac_write_le32(ack + 20u, 0u);
    ac_write_le16(ack + 24u, ac_read_le16(frame + 24u));
    ac_write_le16(ack + 26u, ac_read_le16(frame + 26u));
    ac_write_le32(ack + 28u, 0u);
    header_crc = ac_crc32(ack, 32u);
    ac_write_le32(ack + 32u, header_crc);
    return datalink->ops->write(datalink->ops->user, ack, sizeof(ack),
                                timeout_ms);
}

int ac_datalink_init(ac_datalink_controller_t* datalink,
                     const audio_controller_datalink_device_ops_t* ops)
{
    if (!datalink)
        return -1;

    memset(datalink, 0, sizeof(*datalink));
    datalink->ops = ops;
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

int ac_datalink_poll(ac_datalink_controller_t* datalink,
                     unsigned int timeout_ms)
{
    unsigned char buffer[1024];
    size_t capacity;
    size_t actual;
    uint16_t flags;
    uint16_t header_size;
    uint32_t expected_header_crc;
    uint32_t expected_payload_crc;
    uint32_t payload_crc;
    uint32_t header_crc;
    unsigned char header_copy[AC_DATALINK_HEADER_SIZE];
    int read_result;

    if (!datalink || !datalink->ops || !datalink->open ||
        !datalink->ops->read)
        return -1;

    capacity = datalink->mtu;
    if (capacity == 0u || capacity > sizeof(buffer))
        capacity = sizeof(buffer);
    actual = 0u;
    read_result = datalink->ops->read(datalink->ops->user, buffer,
                                      capacity, &actual, timeout_ms);
    if (read_result != 0)
        return read_result;
    if (actual < AC_DATALINK_HEADER_SIZE)
        return -1;
    if (buffer[0] != 'A' || buffer[1] != 'S' ||
        buffer[2] != 'D' || buffer[3] != 'L')
        return -1;

    header_size = buffer[5];
    if (header_size != AC_DATALINK_HEADER_SIZE || actual < header_size)
        return -1;
    memcpy(header_copy, buffer, header_size);
    expected_header_crc = ac_read_le32(header_copy + 32u);
    ac_write_le32(header_copy + 32u, 0u);
    header_crc = ac_crc32(header_copy, header_size);
    if (header_crc != expected_header_crc)
        return -1;

    expected_payload_crc = ac_read_le32(buffer + 28u);
    payload_crc = ac_crc32(buffer + header_size, actual - header_size);
    flags = ac_read_le16(buffer + 6u);
    if (payload_crc != expected_payload_crc) {
        (void)ac_datalink_send_ack(datalink, buffer, 0, timeout_ms);
        datalink->retries++;
        return -1;
    }

    if ((flags & AC_DATALINK_FLAG_DATA) != 0u) {
        if (ac_datalink_send_ack(datalink, buffer, 1, timeout_ms) != 0)
            return -1;
        datalink->rx_packets++;
    } else if ((flags & AC_DATALINK_FLAG_ACK) != 0u) {
        datalink->tx_packets++;
    } else if ((flags & AC_DATALINK_FLAG_NAK) != 0u) {
        datalink->retries++;
    }
    return 0;
}
