#include "audio_controller.h"
#include "ac_log.h"

#include <assert.h>
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
    return 0;
}
