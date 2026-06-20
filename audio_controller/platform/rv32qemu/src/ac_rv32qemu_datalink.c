#include "ac_rv32qemu_datalink.h"

#include <stdio.h>
#include <string.h>

static int ac_rv32qemu_touch_file(const char* path)
{
    FILE* file;

    if (!path || !path[0])
        return -1;

    file = fopen(path, "ab");
    if (!file)
        return -1;
    fclose(file);
    return 0;
}

static int ac_rv32qemu_open(void* user)
{
    ac_rv32qemu_datalink_t* datalink;

    datalink = (ac_rv32qemu_datalink_t*)user;
    if (!datalink)
        return -1;
    if (ac_rv32qemu_touch_file(datalink->rx_path) != 0)
        return -1;
    if (ac_rv32qemu_touch_file(datalink->tx_path) != 0)
        return -1;
    datalink->rx_offset = 0u;
    datalink->open = 1;
    return 0;
}

static void ac_rv32qemu_close(void* user)
{
    ac_rv32qemu_datalink_t* datalink;

    datalink = (ac_rv32qemu_datalink_t*)user;
    if (datalink)
        datalink->open = 0;
}

static int ac_rv32qemu_read(void* user, void* buffer, size_t capacity,
                            size_t* actual_size, unsigned int timeout_ms)
{
    ac_rv32qemu_datalink_t* datalink;
    FILE* file;
    size_t count;

    (void)timeout_ms;
    if (!actual_size)
        return -1;
    *actual_size = 0u;
    datalink = (ac_rv32qemu_datalink_t*)user;
    if (!datalink || !datalink->open || (!buffer && capacity > 0u))
        return -1;

    file = fopen(datalink->rx_path, "rb");
    if (!file)
        return -1;
    if (fseek(file, (long)datalink->rx_offset, SEEK_SET) != 0) {
        fclose(file);
        return -1;
    }
    count = fread(buffer, 1u, capacity, file);
    fclose(file);
    datalink->rx_offset += count;
    *actual_size = count;
    return count == 0u ? 1 : 0;
}

static int ac_rv32qemu_write(void* user, const void* data, size_t size,
                             unsigned int timeout_ms)
{
    ac_rv32qemu_datalink_t* datalink;
    FILE* file;
    size_t written;

    (void)timeout_ms;
    datalink = (ac_rv32qemu_datalink_t*)user;
    if (!datalink || !datalink->open || (!data && size > 0u))
        return -1;

    file = fopen(datalink->tx_path, "ab");
    if (!file)
        return -1;
    written = fwrite(data, 1u, size, file);
    fclose(file);
    return written == size ? 0 : -1;
}

static size_t ac_rv32qemu_mtu(void* user)
{
    ac_rv32qemu_datalink_t* datalink;

    datalink = (ac_rv32qemu_datalink_t*)user;
    if (!datalink || datalink->mtu == 0u)
        return 256u;
    return datalink->mtu;
}

void ac_rv32qemu_datalink_init(ac_rv32qemu_datalink_t* datalink,
                               const char* rx_path,
                               const char* tx_path,
                               size_t mtu)
{
    if (!datalink)
        return;

    memset(datalink, 0, sizeof(*datalink));
    datalink->rx_path = rx_path;
    datalink->tx_path = tx_path;
    datalink->mtu = mtu == 0u ? 256u : mtu;
    datalink->ops.user = datalink;
    datalink->ops.open = ac_rv32qemu_open;
    datalink->ops.close = ac_rv32qemu_close;
    datalink->ops.read = ac_rv32qemu_read;
    datalink->ops.write = ac_rv32qemu_write;
    datalink->ops.mtu = ac_rv32qemu_mtu;
}

const audio_controller_datalink_device_ops_t*
ac_rv32qemu_datalink_ops(ac_rv32qemu_datalink_t* datalink)
{
    if (!datalink)
        return 0;
    return &datalink->ops;
}
