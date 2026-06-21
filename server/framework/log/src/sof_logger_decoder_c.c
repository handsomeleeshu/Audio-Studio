#include "sof_logger_decoder_c.h"

#include "convert.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AUDIO_STUDIO_SOF_LOGGER_DEFAULT_CLOCK_MHZ 19.2

static struct convert_config audio_studio_global_config;
struct convert_config * const global_config = &audio_studio_global_config;

int audio_studio_sof_logger_decode_file(const char* input_path,
                                        const char* ldc_path,
                                        const char* output_path)
{
    struct convert_config config;
    int ret;

    if (!input_path || !ldc_path || !output_path)
        return -EINVAL;

    memset(&config, 0, sizeof(config));
    config.clock = AUDIO_STUDIO_SOF_LOGGER_DEFAULT_CLOCK_MHZ;
    config.in_file = input_path;
    config.out_file = output_path;
    config.ldc_file = ldc_path;
    config.version_fw = 0;
    config.use_colors = 0;
    config.serial_fd = -EINVAL;
    config.raw_output = 0;
    config.dump_ldc = 0;
    config.hide_location = 0;
    config.relative_timestamps = 1;
    config.time_precision = 6;

    config.ldc_fd = fopen(config.ldc_file, "rb");
    if (!config.ldc_fd)
        return -errno;
    config.in_fd = fopen(config.in_file, "rb");
    if (!config.in_fd) {
        ret = -errno;
        fclose(config.ldc_fd);
        return ret;
    }
    config.out_fd = fopen(config.out_file, "w");
    if (!config.out_fd) {
        ret = -errno;
        fclose(config.in_fd);
        fclose(config.ldc_fd);
        return ret;
    }

    audio_studio_global_config = config;
    ret = convert();
    if (audio_studio_global_config.logs_header) {
        free(audio_studio_global_config.logs_header);
        audio_studio_global_config.logs_header = NULL;
    }

    fclose(config.out_fd);
    fclose(config.in_fd);
    fclose(config.ldc_fd);
    audio_studio_global_config.out_fd = NULL;
    audio_studio_global_config.in_fd = NULL;
    audio_studio_global_config.ldc_fd = NULL;

    return ret;
}
