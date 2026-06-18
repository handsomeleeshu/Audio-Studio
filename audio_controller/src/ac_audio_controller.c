#include "ac_audio_controller_internal.h"

#include <string.h>

static void ac_copy_string(char* dst, size_t dst_size, const char* src)
{
    size_t i;

    if (!dst || dst_size == 0u)
        return;

    if (!src)
        src = "";

    i = 0u;
    while (src[i] != '\0' && i + 1u < dst_size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void ac_append_string(char* dst, size_t dst_size, const char* src)
{
    size_t len;

    if (!dst || dst_size == 0u)
        return;

    len = strlen(dst);
    if (len >= dst_size)
        return;

    ac_copy_string(dst + len, dst_size - len, src);
}

static void ac_append_uint(char* dst, size_t dst_size, unsigned int value)
{
    char tmp[16];
    size_t pos;

    pos = sizeof(tmp);
    pos--;
    tmp[pos] = '\0';

    do {
        pos--;
        tmp[pos] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && pos > 0u);

    ac_append_string(dst, dst_size, &tmp[pos]);
}

static void ac_log_error(audio_controller_t* controller)
{
    if (controller->driver.log)
        controller->driver.log(controller->driver.user,
                               AUDIO_CONTROLLER_LOG_ERROR,
                               controller->last_error);
}

void ac_report_error(audio_controller_t* controller, const char* message)
{
    if (!controller)
        return;

    ac_copy_string(controller->last_error, sizeof(controller->last_error),
                   message);

    ac_log_error(controller);
}

void ac_report_error_detail(audio_controller_t* controller,
                            const char* message,
                            const char* detail)
{
    if (!controller)
        return;

    controller->last_error[0] = '\0';
    ac_append_string(controller->last_error, sizeof(controller->last_error),
                     message);
    ac_append_string(controller->last_error, sizeof(controller->last_error),
                     detail);

    ac_log_error(controller);
}

void ac_report_error_int(audio_controller_t* controller,
                         const char* message,
                         int value)
{
    unsigned int abs_value;

    if (!controller)
        return;

    controller->last_error[0] = '\0';
    ac_append_string(controller->last_error, sizeof(controller->last_error),
                     message);
    if (value < 0) {
        ac_append_string(controller->last_error, sizeof(controller->last_error),
                         "-");
        abs_value = (unsigned int)(0 - value);
    } else {
        abs_value = (unsigned int)value;
    }
    ac_append_uint(controller->last_error, sizeof(controller->last_error),
                   abs_value);

    ac_log_error(controller);
}

audio_controller_t*
audio_controller_create(const audio_controller_create_params_t* params)
{
    const audio_controller_driver_ops_t* driver;
    audio_controller_t* controller;

    if (!params || !params->driver)
        return 0;

    driver = params->driver;
    if (!driver->alloc || !driver->free)
        return 0;

    controller = (audio_controller_t*)driver->alloc(driver->user,
                                                    sizeof(*controller),
                                                    sizeof(void*));
    if (!controller)
        return 0;

    memset(controller, 0, sizeof(*controller));
    controller->driver = *driver;
    controller->allocator.user = driver->user;
    controller->allocator.alloc = driver->alloc;
    controller->allocator.free = driver->free;
    controller->verbose = params->verbose;
    ac_topology_init(&controller->topology);

    return controller;
}

void audio_controller_destroy(audio_controller_t* controller)
{
    audio_controller_driver_ops_t driver;

    if (!controller)
        return;

    driver = controller->driver;
    ac_topology_clear(&controller->topology, &controller->allocator);
    driver.free(driver.user, controller);
}

int audio_controller_load_topology_buffer(audio_controller_t* controller,
                                          const void* data,
                                          size_t size)
{
    char error[256];
    int ret;

    if (!controller || !data || size == 0u)
        return -1;

    error[0] = '\0';
    ret = ac_parse_topology(data, size, &controller->allocator,
                            &controller->topology, error, sizeof(error));
    if (ret != 0) {
        if (error[0] != '\0')
            ac_report_error(controller, error);
        else
            ac_report_error(controller, "failed to parse topology");
        return -1;
    }

    controller->last_error[0] = '\0';
    return 0;
}

int audio_controller_list_pipelines(audio_controller_t* controller,
                                    char* buffer,
                                    size_t buffer_size)
{
    int ret;

    if (!controller || !buffer || buffer_size == 0u)
        return -1;

    ret = ac_topology_format_list(&controller->topology, buffer, buffer_size);
    if (ret != 0) {
        ac_report_error(controller, "pipeline list output buffer is too small");
        return -1;
    }

    return 0;
}

int audio_controller_get_summary(audio_controller_t* controller,
                                 audio_controller_topology_summary_t* summary)
{
    if (!controller || !summary)
        return -1;

    *summary = controller->topology.summary;
    return 0;
}

const char* audio_controller_get_last_error(audio_controller_t* controller)
{
    if (!controller)
        return "audio_controller is null";

    if (controller->last_error[0] == '\0')
        return "";

    return controller->last_error;
}
