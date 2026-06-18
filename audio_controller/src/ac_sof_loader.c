#include "ac_audio_controller_internal.h"
#include "ac_topology_uapi.h"

#include <ipc/control.h>
#include <sof-pipeline.h>

#include <stdint.h>
#include <string.h>

#define AC_MAX_ROUTE_IDS 8u
#define AC_PIPELINE_COMP_ID_STRIDE 256u
#define AC_SOF_MEM_ALIGN 4u

struct ac_sof_mem_header {
    audio_controller_driver_ops_t driver;
    void* raw;
};

static audio_controller_driver_ops_t g_ac_sof_alloc_driver;

static uintptr_t ac_align_up_uintptr(uintptr_t value, uint32_t alignment)
{
    uintptr_t mask;

    if (alignment == 0u)
        return value;

    mask = (uintptr_t)alignment - 1u;
    return (value + mask) & ~mask;
}

static void* ac_sof_mem_alloc(size_t size, uint32_t alignment)
{
    size_t total;
    void* raw;
    uintptr_t base;
    uintptr_t aligned;
    struct ac_sof_mem_header* header;

    if (!g_ac_sof_alloc_driver.alloc)
        return 0;

    if (alignment < AC_SOF_MEM_ALIGN)
        alignment = AC_SOF_MEM_ALIGN;

    total = size + sizeof(*header) + (size_t)alignment;
    raw = g_ac_sof_alloc_driver.alloc(g_ac_sof_alloc_driver.user, total,
                                      alignment);
    if (!raw)
        return 0;

    base = (uintptr_t)raw + sizeof(*header);
    aligned = ac_align_up_uintptr(base, alignment);
    header = (struct ac_sof_mem_header*)(void*)(aligned - sizeof(*header));
    header->driver = g_ac_sof_alloc_driver;
    header->raw = raw;

    memset((void*)aligned, 0, size);
    return (void*)aligned;
}

static void ac_sof_mem_free(void* ptr)
{
    struct ac_sof_mem_header* header;

    if (!ptr)
        return;

    header = (struct ac_sof_mem_header*)((uint8_t*)ptr - sizeof(*header));
    if (header->driver.free)
        header->driver.free(header->driver.user, header->raw);
}

static int ac_streq(const char* a, const char* b)
{
    if (!a || !b)
        return 0;

    return strcmp(a, b) == 0;
}

static int ac_parse_uint_text(const char* text, uint32_t* value)
{
    uint32_t result;
    size_t i;

    if (!text || text[0] == '\0' || !value)
        return -1;

    result = 0u;
    for (i = 0u; text[i] != '\0'; i++) {
        if (text[i] < '0' || text[i] > '9')
            return -1;
        result = result * 10u + (uint32_t)(text[i] - '0');
    }

    *value = result;
    return 0;
}

static uint32_t ac_token_value(const ac_widget_t* widget,
                               uint32_t token,
                               uint32_t fallback)
{
    const ac_token_t* item;

    item = ac_find_token(&widget->tokens, token);
    if (!item || item->type != AC_TOKEN_VALUE)
        return fallback;

    return item->value;
}

static const char* ac_token_string(const ac_widget_t* widget, uint32_t token)
{
    const ac_token_t* item;

    item = ac_find_token(&widget->tokens, token);
    if (!item || item->type != AC_TOKEN_STRING)
        return 0;

    return item->string;
}

static int ac_token_uuid(const ac_widget_t* widget, uint8_t uuid[SOF_UUID_SIZE])
{
    const ac_token_t* item;

    item = ac_find_token(&widget->tokens, AC_SOF_TKN_COMP_UUID);
    if (!item || item->type != AC_TOKEN_UUID)
        return 0;

    memcpy(uuid, item->uuid, SOF_UUID_SIZE);
    return 1;
}

static int ac_process_type_from_string(const char* process_type)
{
    if (!process_type)
        return SOF_COMP_MODULE_ADAPTER;
    if (ac_streq(process_type, "DCBLOCK"))
        return SOF_COMP_DCBLOCK;
    if (ac_streq(process_type, "EQIIR"))
        return SOF_COMP_EQ_IIR;
    if (ac_streq(process_type, "EQFIR"))
        return SOF_COMP_EQ_FIR;
    if (ac_streq(process_type, "KPB"))
        return SOF_COMP_KPB;
    if (ac_streq(process_type, "CHAN_SELECTOR"))
        return SOF_COMP_SELECTOR;
    if (ac_streq(process_type, "MUX"))
        return SOF_COMP_MUX;
    if (ac_streq(process_type, "DEMUX"))
        return SOF_COMP_DEMUX;
    if (ac_streq(process_type, "DYNAMIC_PROCESS"))
        return SOF_COMP_MODULE_ADAPTER;

    return SOF_COMP_MODULE_ADAPTER;
}

static int ac_dai_type_from_string(const char* dai_type)
{
    if (!dai_type)
        return -1;
    if (ac_streq(dai_type, "FILE_IO") || ac_streq(dai_type, "file_io_dai"))
        return SOF_DAI_FILE_IO;
    if (ac_streq(dai_type, "VSI_TDM") || ac_streq(dai_type, "vsi_tdm_dai"))
        return SOF_DAI_VSI_TDM;
    if (ac_streq(dai_type, "VSI_I2S") || ac_streq(dai_type, "vsi_i2s_dai"))
        return SOF_DAI_VSI_I2S;
    if (ac_streq(dai_type, "DW_I2S") || ac_streq(dai_type, "dw_i2s_dai"))
        return SOF_DAI_DW_I2S;
    if (ac_streq(dai_type, "DW_TDM") || ac_streq(dai_type, "dw_tdm_dai"))
        return SOF_DAI_DW_TDM;

    return -1;
}

static int ac_comp_type_for_widget(const ac_widget_t* widget)
{
    switch (widget->id) {
    case AC_DAPM_AIF_IN:
    case AC_DAPM_AIF_OUT:
        return SOF_COMP_HOST;
    case AC_DAPM_DAI_IN:
    case AC_DAPM_DAI_OUT:
        return SOF_COMP_DAI;
    case AC_DAPM_PGA:
        return SOF_COMP_VOLUME;
    case AC_DAPM_MIXER:
        return SOF_COMP_MIXER;
    case AC_DAPM_MUX:
        return SOF_COMP_MUX;
    case AC_DAPM_SRC:
        return SOF_COMP_SRC;
    case AC_DAPM_ASRC:
        return SOF_COMP_ASRC;
    case AC_DAPM_EFFECT:
        return ac_process_type_from_string(
            ac_token_string(widget, AC_SOF_TKN_PROCESS_TYPE));
    default:
        break;
    }

    return SOF_COMP_NONE;
}

static int ac_widget_index_by_name(const ac_topology_t* topology,
                                   const char* name)
{
    size_t i;

    for (i = 0u; i < topology->widget_count; i++) {
        if (strcmp(topology->widgets[i].name, name) == 0)
            return (int)i;
    }

    return -1;
}

static int ac_add_unique_id(uint16_t ids[], uint16_t* count, uint16_t id)
{
    uint16_t i;

    if (id == 0u)
        return 0;

    for (i = 0u; i < *count; i++) {
        if (ids[i] == id)
            return 0;
    }

    if (*count >= AC_MAX_ROUTE_IDS)
        return -1;

    ids[*count] = id;
    (*count)++;
    return 0;
}

static int ac_collect_route_ids(const ac_topology_t* topology,
                                const char* widget_name,
                                uint32_t pipeline_id,
                                const uint16_t* comp_ids,
                                int source_side,
                                uint16_t ids[],
                                uint16_t* count,
                                uint32_t depth)
{
    size_t i;
    const ac_route_t* route;
    const char* other_name;
    int other_index;
    const ac_widget_t* other_widget;
    int other_type;
    int ret;

    if (depth > topology->widget_count)
        return -1;

    for (i = 0u; i < topology->route_count; i++) {
        route = &topology->routes[i];
        other_name = 0;
        if (source_side) {
            if (strcmp(route->sink, widget_name) == 0)
                other_name = route->source;
        } else {
            if (strcmp(route->source, widget_name) == 0)
                other_name = route->sink;
        }
        if (!other_name || other_name[0] == '\0')
            continue;

        other_index = ac_widget_index_by_name(topology, other_name);
        if (other_index < 0)
            continue;

        other_widget = &topology->widgets[other_index];
        if (other_widget->pipeline_id != pipeline_id)
            continue;

        other_type = ac_comp_type_for_widget(other_widget);
        if (other_widget->id == AC_DAPM_BUFFER ||
            other_type == SOF_COMP_NONE) {
            ret = ac_collect_route_ids(topology, other_widget->name,
                                       pipeline_id, comp_ids, source_side,
                                       ids, count, depth + 1u);
        } else {
            ret = ac_add_unique_id(ids, count,
                                   comp_ids[(size_t)other_index]);
        }
        if (ret != 0)
            return ret;
    }

    return 0;
}

static void ac_fill_file_io_defaults(struct sof_ipc_dai_config* dai,
                                     const ac_pipeline_t* pipeline)
{
    uint32_t i;

    dai->type = SOF_DAI_FILE_IO;
    dai->file_io.sample_rate = 48000u;
    dai->file_io.channels = pipeline->max_channels ?
        pipeline->max_channels : 2u;
    dai->file_io.sample_width = 16u;
    dai->format = SOF_DAI_FMT_I2S | SOF_DAI_FMT_GATED |
        SOF_DAI_FMT_NB_NF | SOF_DAI_FMT_CBC_CFP;

    for (i = 0u; i < SOF_IPC_MAX_CHANNELS; i++)
        dai->file_io.chmap[i] = SOF_CHMAP_UNKNOWN;
}

static int ac_fill_comp_config(const ac_widget_t* widget,
                               const ac_pipeline_t* pipeline,
                               uint16_t comp_id,
                               struct sof_comp_config* config)
{
    int dai_type;

    memset(config, 0, sizeof(*config));
    config->comp_id = comp_id;
    config->name = widget->name;
    config->core = ac_token_value(widget, AC_SOF_TKN_COMP_CORE_ID, 0u);
    config->type = ac_comp_type_for_widget(widget);
    config->periods_sink = (uint16_t)ac_token_value(
        widget, AC_SOF_TKN_COMP_PERIOD_SINK_COUNT, 2u);
    config->periods_source = (uint16_t)ac_token_value(
        widget, AC_SOF_TKN_COMP_PERIOD_SOURCE_COUNT, 2u);
    config->has_uuid = ac_token_uuid(widget, config->uuid);

    switch (config->type) {
    case SOF_COMP_HOST:
        config->host.stream_name = widget->stream_name;
        break;
    case SOF_COMP_DAI:
        dai_type = ac_dai_type_from_string(
            ac_token_string(widget, AC_SOF_TKN_DAI_TYPE));
        if (dai_type < 0)
            return -1;
        if (dai_type == SOF_DAI_FILE_IO)
            ac_fill_file_io_defaults(&config->dai, pipeline);
        else
            config->dai.type = (uint32_t)dai_type;
        config->dai.dai_index = ac_token_value(widget,
                                               AC_SOF_TKN_DAI_INDEX, 0u);
        break;
    case SOF_COMP_VOLUME:
        config->volume.channels = pipeline->max_channels ?
            pipeline->max_channels : 2u;
        config->volume.min_value = 0u;
        config->volume.max_value = 65536u;
        config->volume.ramp = SOF_VOLUME_LINEAR;
        config->volume.initial_ramp = 250u;
        break;
    case SOF_COMP_SRC:
        config->src.source_rate = ac_token_value(widget,
                                                 AC_SOF_TKN_SRC_RATE_IN, 0u);
        config->src.sink_rate = ac_token_value(widget,
                                               AC_SOF_TKN_SRC_RATE_OUT, 0u);
        break;
    case SOF_COMP_ASRC:
        config->asrc.source_rate = ac_token_value(widget,
                                                  AC_SOF_TKN_ASRC_RATE_IN, 0u);
        config->asrc.sink_rate = ac_token_value(widget,
                                                AC_SOF_TKN_ASRC_RATE_OUT, 0u);
        config->asrc.asynchronous_mode = ac_token_value(
            widget, AC_SOF_TKN_ASRC_ASYNCHRONOUS_MODE, 0u);
        config->asrc.operation_mode = ac_token_value(
            widget, AC_SOF_TKN_ASRC_OPERATION_MODE, 0u);
        break;
    default:
        config->def.size = 0u;
        config->def.type = 0u;
        config->def.data = 0;
        break;
    }

    return 0;
}

static const ac_widget_t* ac_find_scheduler(const ac_topology_t* topology,
                                            const ac_pipeline_t* pipeline)
{
    size_t i;
    const ac_widget_t* widget;

    for (i = 0u; i < topology->widget_count; i++) {
        widget = &topology->widgets[i];
        if (widget->pipeline_id == pipeline->id &&
            widget->id == AC_DAPM_SCHEDULER)
            return widget;
    }

    return 0;
}

static void ac_clear_installed(audio_controller_installed_pipelines_t* installed)
{
    if (installed)
        memset(installed, 0, sizeof(*installed));
}

static int ac_config_pipe(struct sof_pipe* pipe,
                          const ac_pipeline_t* pipeline,
                          const ac_widget_t* scheduler,
                          uint32_t last_sub_pipe)
{
    struct sof_pipe_config pipe_config;
    int ret;

    memset(&pipe_config, 0, sizeof(pipe_config));
    pipe_config.pipe_idx = pipeline->id;
    pipe_config.core = ac_token_value(scheduler, AC_SOF_TKN_SCHED_CORE, 0u);
    pipe_config.period = ac_token_value(scheduler,
                                        AC_SOF_TKN_SCHED_PERIOD, 4000u);
    pipe_config.priority = (uint8_t)ac_token_value(
        scheduler, AC_SOF_TKN_SCHED_PRIORITY, 0u);
    pipe_config.channel_max = (uint8_t)pipeline->max_channels;
    pipe_config.bytes_per_sample_max =
        (uint8_t)pipeline->max_bytes_per_sample;
    pipe_config.sample_rate_max = pipeline->max_sample_rate;
    pipe_config.pipe_sched_id = 0u;
    pipe_config.last_sub_pipe = last_sub_pipe ? 1 : 0;

    ret = sof_pipeline_config(pipe, PIPE_CFG_PIPE_INDEX, &pipe_config);
    ret |= sof_pipeline_config(pipe, PIPE_CFG_PIPE_CORE_ID, &pipe_config);
    ret |= sof_pipeline_config(pipe, PIPE_CFG_PERIOD, &pipe_config);
    ret |= sof_pipeline_config(pipe, PIPE_CFG_PRIORITY, &pipe_config);
    ret |= sof_pipeline_config(pipe, PIPE_CFG_CHANNEL_MAX, &pipe_config);
    ret |= sof_pipeline_config(pipe, PIPE_CFG_BYTES_PER_SAMPLE_MAX,
                               &pipe_config);
    ret |= sof_pipeline_config(pipe, PIPE_CFG_SAMPLE_RATE_MAX, &pipe_config);
    ret |= sof_pipeline_config(pipe, PIPE_CFG_PIPE_SCHED_ID, &pipe_config);
    ret |= sof_pipeline_config(pipe, PIPE_CFG_LAST_SUB_PIPE, &pipe_config);

    return ret == 0 ? 0 : -1;
}

static int ac_alloc_comp_ids(audio_controller_t* controller,
                             const ac_pipeline_t* pipeline,
                             uint16_t** ids_out)
{
    const ac_topology_t* topology;
    uint16_t* comp_ids;
    uint16_t next_id;
    uint32_t base_id;
    size_t size;
    size_t i;
    int type;

    topology = &controller->topology;
    size = topology->widget_count * sizeof(uint16_t);
    comp_ids = (uint16_t*)controller->driver.alloc(controller->driver.user,
                                                   size, sizeof(uint16_t));
    if (!comp_ids)
        return -1;

    memset(comp_ids, 0, size);
    base_id = pipeline->id * AC_PIPELINE_COMP_ID_STRIDE + 1u;
    if (base_id > UINT16_MAX) {
        controller->driver.free(controller->driver.user, comp_ids);
        return -1;
    }
    next_id = (uint16_t)base_id;
    for (i = 0u; i < topology->widget_count; i++) {
        if (topology->widgets[i].pipeline_id != pipeline->id)
            continue;
        type = ac_comp_type_for_widget(&topology->widgets[i]);
        if (type != SOF_COMP_NONE) {
            if (next_id == UINT16_MAX) {
                controller->driver.free(controller->driver.user, comp_ids);
                return -1;
            }
            comp_ids[i] = next_id++;
        }
    }

    *ids_out = comp_ids;
    return 0;
}

static void ac_destroy_pipe(struct sof_pipe* pipe)
{
    if (pipe) {
        (void)sof_pipeline_remove(pipe);
        (void)sof_pipeline_destroy(pipe);
    }
}

static int ac_install_one_pipeline(audio_controller_t* controller,
                                   const ac_pipeline_t* pipeline,
                                   uint32_t last_sub_pipe,
                                   audio_controller_installed_pipeline_t* out)
{
    const ac_topology_t* topology;
    const ac_widget_t* scheduler;
    struct sof_mem_ops mem_ops;
    struct sof_pipe* pipe;
    uint16_t* comp_ids;
    size_t i;
    int ret;

    topology = &controller->topology;
    scheduler = ac_find_scheduler(topology, pipeline);
    if (!scheduler) {
        ac_report_error_detail(controller, "pipeline has no scheduler: ",
                               pipeline->name);
        return -1;
    }

    if (ac_alloc_comp_ids(controller, pipeline, &comp_ids) != 0) {
        ac_report_error(controller, "out of memory while building install plan");
        return -1;
    }

    g_ac_sof_alloc_driver = controller->driver;
    mem_ops.smalloc = ac_sof_mem_alloc;
    mem_ops.sfree = ac_sof_mem_free;
    pipe = sof_pipeline_create(&mem_ops);
    if (!pipe) {
        controller->driver.free(controller->driver.user, comp_ids);
        ac_report_error_detail(controller, "sof_pipeline_create failed: ",
                               pipeline->name);
        return -1;
    }

    if (ac_config_pipe(pipe, pipeline, scheduler, last_sub_pipe) != 0) {
        controller->driver.free(controller->driver.user, comp_ids);
        (void)sof_pipeline_destroy(pipe);
        ac_report_error_detail(controller, "sof_pipeline_config failed: ",
                               pipeline->name);
        return -1;
    }

    for (i = 0u; i < topology->widget_count; i++) {
        const ac_widget_t* widget;
        struct sof_comp_config comp_config;
        uint16_t src_ids[AC_MAX_ROUTE_IDS];
        uint16_t sink_ids[AC_MAX_ROUTE_IDS];
        uint16_t src_count;
        uint16_t sink_count;

        if (comp_ids[i] == 0u)
            continue;

        widget = &topology->widgets[i];
        src_count = 0u;
        sink_count = 0u;
        memset(src_ids, 0, sizeof(src_ids));
        memset(sink_ids, 0, sizeof(sink_ids));

        ret = ac_collect_route_ids(topology, widget->name, pipeline->id,
                                   comp_ids, 1, src_ids, &src_count, 0u);
        if (ret == 0)
            ret = ac_collect_route_ids(topology, widget->name, pipeline->id,
                                       comp_ids, 0, sink_ids, &sink_count,
                                       0u);
        if (ret != 0) {
            controller->driver.free(controller->driver.user, comp_ids);
            ac_destroy_pipe(pipe);
            ac_report_error_detail(controller, "too many routes for component: ",
                                   widget->name);
            return -1;
        }

        if (ac_fill_comp_config(widget, pipeline, comp_ids[i],
                                &comp_config) != 0) {
            controller->driver.free(controller->driver.user, comp_ids);
            ac_destroy_pipe(pipe);
            ac_report_error_detail(controller, "unsupported component config: ",
                                   widget->name);
            return -1;
        }

        ret = sof_pipeline_add_comp(pipe, &comp_config,
                                    src_count ? src_ids : 0, src_count,
                                    sink_count ? sink_ids : 0, sink_count);
        if (ret < 0) {
            controller->driver.free(controller->driver.user, comp_ids);
            ac_destroy_pipe(pipe);
            ac_report_error_detail(controller, "sof_pipeline_add_comp failed: ",
                                   widget->name);
            return -1;
        }
    }

    ret = sof_pipeline_install(pipe);
    controller->driver.free(controller->driver.user, comp_ids);
    if (ret < 0) {
        ac_destroy_pipe(pipe);
        ac_report_error_int(controller, "sof_pipeline_install failed: ", ret);
        return -1;
    }

    memset(out, 0, sizeof(*out));
    out->pipe_id = pipeline->id;
    out->max_sample_rate = pipeline->max_sample_rate;
    out->max_channels = pipeline->max_channels;
    out->pipeline = pipe;

    return 0;
}

static void ac_cleanup_installed(audio_controller_installed_pipelines_t* installed)
{
    uint32_t i;

    if (!installed)
        return;

    for (i = 0u; i < installed->count; i++) {
        if (installed->pipelines[i].pipeline) {
            ac_destroy_pipe(installed->pipelines[i].pipeline);
            installed->pipelines[i].pipeline = 0;
        }
    }
    installed->count = 0u;
}

int audio_controller_install_pipeline(audio_controller_t* controller,
                                      const char* id_or_name,
                                      audio_controller_installed_pipelines_t*
                                          installed)
{
    size_t i;
    uint32_t requested_id;
    int has_id;
    const ac_pipeline_t* pipeline;

    if (!controller || !installed)
        return -1;
    if (!id_or_name || id_or_name[0] == '\0') {
        ac_report_error(controller, "pipeline id/name is empty");
        return -1;
    }

    ac_clear_installed(installed);
    requested_id = 0u;
    has_id = ac_parse_uint_text(id_or_name, &requested_id) == 0 ? 1 : 0;
    pipeline = 0;

    for (i = 0u; i < controller->topology.pipeline_count; i++) {
        if ((has_id && controller->topology.pipelines[i].id == requested_id) ||
            strcmp(controller->topology.pipelines[i].name, id_or_name) == 0) {
            pipeline = &controller->topology.pipelines[i];
            break;
        }
    }

    if (!pipeline) {
        ac_report_error_detail(controller, "pipeline not found: ", id_or_name);
        return -1;
    }

    installed->pipe_id = pipeline->id;
    installed->count = 1u;
    if (ac_install_one_pipeline(controller, pipeline, 1u,
                                &installed->pipelines[0]) != 0) {
        ac_cleanup_installed(installed);
        return -1;
    }

    return 0;
}

int audio_controller_install_all(audio_controller_t* controller,
                                 audio_controller_installed_pipelines_t*
                                     installed)
{
    size_t i;
    uint32_t last;

    if (!controller || !installed)
        return -1;

    ac_clear_installed(installed);
    if (controller->topology.pipeline_count >
        AUDIO_CONTROLLER_MAX_INSTALLED_PIPELINES) {
        ac_report_error(controller, "too many pipelines in topology");
        return -1;
    }

    if (controller->topology.pipeline_count == 0u) {
        ac_report_error(controller, "topology has no pipelines");
        return -1;
    }

    installed->pipe_id = controller->topology.pipelines[0].id;
    for (i = 0u; i < controller->topology.pipeline_count; i++) {
        last = (i + 1u == controller->topology.pipeline_count) ? 1u : 0u;
        if (ac_install_one_pipeline(controller,
                                    &controller->topology.pipelines[i],
                                    last,
                                    &installed->pipelines[i]) != 0) {
            ac_cleanup_installed(installed);
            return -1;
        }
        installed->count++;
    }

    return 0;
}
