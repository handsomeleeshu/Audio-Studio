#include "audio_controller.h"
#include "ac_rv32qemu_datalink.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct test_driver_state {
    int datalink_opened;
    int datalink_closed;
    unsigned int writes;
};

static void* test_alloc(void* user, size_t size, size_t alignment)
{
    (void)user;
    (void)alignment;
    return calloc(1u, size == 0u ? 1u : size);
}

static void test_free(void* user, void* ptr)
{
    (void)user;
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

static void test_rv32qemu_datalink_ops(void)
{
    const char* rx_path;
    const char* tx_path;
    ac_rv32qemu_datalink_t datalink;
    const audio_controller_datalink_device_ops_t* ops;
    const unsigned char payload[3] = {1u, 2u, 3u};

    rx_path = "ac_rv32qemu_test.rx";
    tx_path = "ac_rv32qemu_test.tx";
    remove(rx_path);
    remove(tx_path);

    ac_rv32qemu_datalink_init(&datalink, rx_path, tx_path, 91u);
    ops = ac_rv32qemu_datalink_ops(&datalink);
    assert(ops);
    assert(ops->mtu(ops->user) == 91u);
    assert(ops->open(ops->user) == 0);
    assert(ops->write(ops->user, payload, sizeof(payload), 10u) == 0);
    ops->close(ops->user);
    assert(!datalink.open);

    remove(rx_path);
    remove(tx_path);
}

int main(void)
{
    struct test_driver_state state;
    audio_controller_datalink_device_ops_t datalink;
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

    memset(&driver, 0, sizeof(driver));
    driver.user = &state;
    driver.alloc = test_alloc;
    driver.free = test_free;
    driver.log = test_log;
    driver.datalink = &datalink;

    memset(&params, 0, sizeof(params));
    params.driver = &driver;

    controller = audio_controller_create(&params);
    assert(controller);
    assert(audio_controller_get_transport_stats(controller, &stats) == 0);
    assert(stats.initialized);
    assert(stats.datalink_open);
    assert(stats.datalink_mtu == 37u);
    assert(state.datalink_opened == 1);

    audio_controller_destroy(controller);
    assert(state.datalink_closed == 1);
    test_rv32qemu_datalink_ops();
    return 0;
}
