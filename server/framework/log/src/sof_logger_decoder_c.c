#include "sof_logger_decoder_c.h"

#include "convert.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <user/abi_dbg.h>
#include <user/trace.h>

#define AUDIO_STUDIO_SOF_LOGGER_DEFAULT_CLOCK_MHZ 19.2
#define AUDIO_STUDIO_TRACE_MAX_PARAMS_COUNT 4u

static struct convert_config audio_studio_global_config;
struct convert_config * const global_config = &audio_studio_global_config;

struct audio_studio_ldc_entry_header {
    uint32_t level;
    uint32_t component_class;
    uint32_t params_num;
    uint32_t line_idx;
    uint32_t file_name_len;
    uint32_t text_len;
};

struct audio_studio_sof_logger_decoder {
    char *ldc_path;
    FILE *ldc_fd;
    struct snd_sof_logs_header logs_header;
};

static void audio_studio_sof_logger_init_config(struct convert_config *config,
                                                const char *input_path,
                                                const char *ldc_path,
                                                const char *output_path)
{
    memset(config, 0, sizeof(*config));
    config->clock = AUDIO_STUDIO_SOF_LOGGER_DEFAULT_CLOCK_MHZ;
    config->in_file = input_path;
    config->out_file = output_path;
    config->ldc_file = ldc_path;
    config->version_fw = 0;
    config->use_colors = 0;
    config->serial_fd = -EINVAL;
    config->raw_output = 0;
    config->dump_ldc = 0;
    config->hide_location = 0;
    config->relative_timestamps = 1;
    config->time_precision = 6;
}

static void audio_studio_sof_logger_clear_global_config(void)
{
    if (audio_studio_global_config.logs_header) {
        free(audio_studio_global_config.logs_header);
        audio_studio_global_config.logs_header = NULL;
    }
    audio_studio_global_config.out_fd = NULL;
    audio_studio_global_config.in_fd = NULL;
    audio_studio_global_config.ldc_fd = NULL;
}

static int audio_studio_sof_logger_read_logs_header(FILE *ldc_fd,
                                                    const char *ldc_path,
                                                    struct snd_sof_logs_header *header)
{
    size_t count;

    if (fseek(ldc_fd, 0, SEEK_SET) != 0)
        return -errno;
    count = fread(header, sizeof(*header), 1, ldc_fd);
    if (count != 1)
        return ferror(ldc_fd) ? -ferror(ldc_fd) : -EINVAL;
    if (strncmp((const char *)header->sig, SND_SOF_LOGS_SIG,
                SND_SOF_LOGS_SIG_SIZE) != 0) {
        (void)ldc_path;
        return -EINVAL;
    }
    if (SOF_ABI_VERSION_INCOMPATIBLE(SOF_ABI_DBG_VERSION,
                                     header->version.abi_version))
        return -EINVAL;
    return 0;
}

int audio_studio_sof_logger_decode_file(const char* input_path,
                                        const char* ldc_path,
                                        const char* output_path)
{
    struct convert_config config;
    int ret;

    if (!input_path || !ldc_path || !output_path)
        return -EINVAL;

    audio_studio_sof_logger_init_config(&config, input_path, ldc_path,
                                        output_path);

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
    audio_studio_sof_logger_clear_global_config();

    fclose(config.out_fd);
    fclose(config.in_fd);
    fclose(config.ldc_fd);

    return ret;
}

int audio_studio_sof_logger_decoder_create(const char* ldc_path,
                                           audio_studio_sof_logger_decoder_t** out)
{
    audio_studio_sof_logger_decoder_t *decoder;
    int ret;

    if (!ldc_path || !out)
        return -EINVAL;
    *out = NULL;

    decoder = (audio_studio_sof_logger_decoder_t *)calloc(1, sizeof(*decoder));
    if (!decoder)
        return -ENOMEM;
    decoder->ldc_path = strdup(ldc_path);
    if (!decoder->ldc_path) {
        free(decoder);
        return -ENOMEM;
    }
    decoder->ldc_fd = fopen(ldc_path, "rb");
    if (!decoder->ldc_fd) {
        ret = -errno;
        audio_studio_sof_logger_decoder_destroy(decoder);
        return ret;
    }

    ret = audio_studio_sof_logger_read_logs_header(decoder->ldc_fd,
                                                  decoder->ldc_path,
                                                  &decoder->logs_header);
    if (ret != 0) {
        audio_studio_sof_logger_decoder_destroy(decoder);
        return ret;
    }
    *out = decoder;
    return 0;
}

void audio_studio_sof_logger_decoder_destroy(audio_studio_sof_logger_decoder_t* decoder)
{
    if (!decoder)
        return;
    if (decoder->ldc_fd)
        fclose(decoder->ldc_fd);
    free(decoder->ldc_path);
    free(decoder);
}

int audio_studio_sof_logger_decoder_record_size(audio_studio_sof_logger_decoder_t* decoder,
                                                const void* data,
                                                unsigned long data_size,
                                                unsigned long* record_size)
{
    struct log_entry_header trace_header;
    struct audio_studio_ldc_entry_header entry_header;
    uint32_t entry_offset;
    size_t count;

    if (!decoder || !data || !record_size)
        return -EINVAL;
    *record_size = 0ul;
    if (data_size < sizeof(trace_header))
        return 1;

    memcpy(&trace_header, data, sizeof(trace_header));
    if (trace_header.log_entry_address < decoder->logs_header.base_address ||
        trace_header.log_entry_address > decoder->logs_header.base_address +
                                         decoder->logs_header.data_length)
        return -ERANGE;

    entry_offset = (trace_header.log_entry_address -
                    decoder->logs_header.base_address) +
                   decoder->logs_header.data_offset;
    if (fseek(decoder->ldc_fd, entry_offset, SEEK_SET) != 0)
        return -errno;
    count = fread(&entry_header, sizeof(entry_header), 1, decoder->ldc_fd);
    if (count != 1)
        return ferror(decoder->ldc_fd) ? -ferror(decoder->ldc_fd) : -EINVAL;
    if (entry_header.params_num > AUDIO_STUDIO_TRACE_MAX_PARAMS_COUNT)
        return -EINVAL;

    *record_size = sizeof(trace_header) +
                   (unsigned long)entry_header.params_num * sizeof(uint32_t);
    if (data_size < *record_size)
        return 1;
    return 0;
}

#if !defined(_WIN32)
static FILE *audio_studio_open_input_buffer(const void *data, unsigned long size)
{
    return fmemopen((void *)data, size, "rb");
}

static FILE *audio_studio_open_output_buffer(char **output, size_t *output_size)
{
    *output = NULL;
    *output_size = 0u;
    return open_memstream(output, output_size);
}

static int audio_studio_finish_output_buffer(FILE *out_fd,
                                             char **output,
                                             size_t *output_size)
{
    if (fclose(out_fd) != 0)
        return -errno;
    if (!*output) {
        *output = (char *)calloc(1, 1);
        if (!*output)
            return -ENOMEM;
        *output_size = 0u;
    }
    return 0;
}
#else
static FILE *audio_studio_open_input_buffer(const void *data, unsigned long size)
{
    FILE *file = tmpfile();
    if (!file)
        return NULL;
    if (size > 0 && fwrite(data, 1, size, file) != size) {
        fclose(file);
        return NULL;
    }
    rewind(file);
    return file;
}

static FILE *audio_studio_open_output_buffer(char **output, size_t *output_size)
{
    *output = NULL;
    *output_size = 0u;
    return tmpfile();
}

static int audio_studio_finish_output_buffer(FILE *out_fd,
                                             char **output,
                                             size_t *output_size)
{
    long size;

    if (fflush(out_fd) != 0) {
        fclose(out_fd);
        return -errno;
    }
    size = ftell(out_fd);
    if (size < 0) {
        fclose(out_fd);
        return -errno;
    }
    rewind(out_fd);
    *output = (char *)malloc((size_t)size + 1u);
    if (!*output) {
        fclose(out_fd);
        return -ENOMEM;
    }
    if (size > 0 && fread(*output, 1, (size_t)size, out_fd) != (size_t)size) {
        free(*output);
        *output = NULL;
        fclose(out_fd);
        return ferror(out_fd) ? -ferror(out_fd) : -EIO;
    }
    (*output)[size] = '\0';
    *output_size = (size_t)size;
    if (fclose(out_fd) != 0)
        return -errno;
    return 0;
}
#endif

int audio_studio_sof_logger_decoder_decode_buffer(audio_studio_sof_logger_decoder_t* decoder,
                                                  const void* data,
                                                  unsigned long data_size,
                                                  char** output,
                                                  unsigned long* output_size)
{
    struct convert_config config;
    FILE *in_fd;
    FILE *out_fd;
    size_t mem_size = 0u;
    int ret;

    if (!decoder || (!data && data_size > 0ul) || !output || !output_size)
        return -EINVAL;
    *output = NULL;
    *output_size = 0ul;
    if (data_size == 0ul) {
        *output = (char *)calloc(1, 1);
        return *output ? 0 : -ENOMEM;
    }

    in_fd = audio_studio_open_input_buffer(data, data_size);
    if (!in_fd)
        return -errno;
    out_fd = audio_studio_open_output_buffer(output, &mem_size);
    if (!out_fd) {
        ret = -errno;
        fclose(in_fd);
        return ret;
    }

    audio_studio_sof_logger_init_config(&config, "memory", decoder->ldc_path,
                                        "memory");
    config.in_fd = in_fd;
    config.out_fd = out_fd;
    config.ldc_fd = decoder->ldc_fd;
    if (fseek(decoder->ldc_fd, 0, SEEK_SET) != 0) {
        ret = -errno;
        fclose(in_fd);
        fclose(out_fd);
        return ret;
    }

    audio_studio_global_config = config;
    ret = convert();
    audio_studio_sof_logger_clear_global_config();
    fclose(in_fd);
    if (audio_studio_finish_output_buffer(out_fd, output, &mem_size) != 0) {
        if (*output) {
            free(*output);
            *output = NULL;
        }
        return ret != 0 ? ret : -EIO;
    }
    *output_size = (unsigned long)mem_size;
    return ret;
}

void audio_studio_sof_logger_decoder_free_output(char* output)
{
    free(output);
}
