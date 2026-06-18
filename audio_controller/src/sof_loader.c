#include "audio_controller_internal.h"
#include "topology_uapi.h"

#ifdef AUDIO_CONTROLLER_WITH_SOF_CLIENT
#include <sof-pipeline.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(__GNUC__)
#define AC_WEAK __attribute__((weak))
#else
#define AC_WEAK
#endif

extern struct sof_pipe* sof_pipeline_create(struct sof_mem_ops* ops) AC_WEAK;
extern int sof_pipeline_config(struct sof_pipe* pipe,
                               enum sof_pipe_config_item item,
                               struct sof_pipe_config* config) AC_WEAK;
extern int sof_pipeline_add_comp(struct sof_pipe* pipe,
                                 struct sof_comp_config* config,
                                 uint16_t src_comp_id[],
                                 uint16_t src_comp_num,
                                 uint16_t sink_comp_id[],
                                 uint16_t sink_comp_num) AC_WEAK;
extern int sof_pipeline_install(struct sof_pipe* pipe) AC_WEAK;
extern int sof_pipeline_destroy(struct sof_pipe* pipe) AC_WEAK;

static audio_controller_t* g_mem_controller;

static void* sof_mem_alloc(size_t size, uint32_t alignment) {
  if (g_mem_controller == NULL) return NULL;
  return g_mem_controller->platform.alloc(g_mem_controller->platform.user, size, alignment);
}

static void sof_mem_free(void* ptr) {
  if (g_mem_controller == NULL) return;
  g_mem_controller->platform.free(g_mem_controller->platform.user, ptr);
}

static int sof_symbols_available(void) {
  return sof_pipeline_create != NULL &&
         sof_pipeline_config != NULL &&
         sof_pipeline_add_comp != NULL &&
         sof_pipeline_install != NULL &&
         sof_pipeline_destroy != NULL;
}

static const ac_widget_t* find_scheduler(const ac_topology_t* topology, const ac_pipeline_t* pipeline) {
  size_t i;
  for (i = 0; i < topology->widget_count; ++i) {
    const ac_widget_t* widget = &topology->widgets[i];
    if (strcmp(widget->pipeline_name, pipeline->name) == 0 && widget->id == AC_DAPM_SCHEDULER) {
      return widget;
    }
  }
  return NULL;
}

static uint32_t token_value(const ac_widget_t* widget, uint32_t token, uint32_t fallback) {
  const ac_token_t* item = ac_find_token(&widget->tokens, token);
  if (item == NULL || item->type != AC_TOKEN_VALUE) return fallback;
  return item->value;
}

static const char* token_string(const ac_widget_t* widget, uint32_t token) {
  const ac_token_t* item = ac_find_token(&widget->tokens, token);
  if (item == NULL || item->type != AC_TOKEN_STRING) return NULL;
  return item->string;
}

static int token_uuid(const ac_widget_t* widget, uint8_t uuid[SOF_UUID_SIZE]) {
  const ac_token_t* item = ac_find_token(&widget->tokens, AC_SOF_TKN_COMP_UUID);
  if (item == NULL || item->type != AC_TOKEN_UUID) return 0;
  memcpy(uuid, item->uuid, SOF_UUID_SIZE);
  return 1;
}

static int process_type_from_string(const char* process_type) {
  if (process_type == NULL) return SOF_COMP_MODULE_ADAPTER;
  if (strcmp(process_type, "DCBLOCK") == 0) return SOF_COMP_DCBLOCK;
  if (strcmp(process_type, "EQIIR") == 0) return SOF_COMP_EQ_IIR;
  if (strcmp(process_type, "EQFIR") == 0) return SOF_COMP_EQ_FIR;
  if (strcmp(process_type, "KPB") == 0) return SOF_COMP_KPB;
  if (strcmp(process_type, "CHAN_SELECTOR") == 0) return SOF_COMP_SELECTOR;
  if (strcmp(process_type, "MUX") == 0) return SOF_COMP_MUX;
  if (strcmp(process_type, "DEMUX") == 0) return SOF_COMP_DEMUX;
  return SOF_COMP_MODULE_ADAPTER;
}

static int dai_type_from_string(const char* dai_type) {
  if (dai_type == NULL) return SOF_DAI_VSI_TDM;
  if (strcmp(dai_type, "VSI_TDM") == 0) return SOF_DAI_VSI_TDM;
  if (strcmp(dai_type, "VSI_I2S") == 0) return SOF_DAI_VSI_I2S;
  if (strcmp(dai_type, "DW_I2S") == 0) return SOF_DAI_DW_I2S;
  if (strcmp(dai_type, "DW_TDM") == 0) return SOF_DAI_DW_TDM;
  if (strcmp(dai_type, "FILE_IO") == 0) return SOF_DAI_FILE_IO;
  return SOF_DAI_VSI_TDM;
}

static int comp_type_for_widget(const ac_widget_t* widget) {
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
      return process_type_from_string(token_string(widget, AC_SOF_TKN_PROCESS_TYPE));
    default:
      return SOF_COMP_NONE;
  }
}

static int find_widget_index_by_name(const ac_topology_t* topology, const char* name) {
  size_t i;
  for (i = 0; i < topology->widget_count; ++i) {
    if (strcmp(topology->widgets[i].name, name) == 0) return (int)i;
  }
  return -1;
}

static void fill_comp_config(const ac_widget_t* widget, uint16_t comp_id, struct sof_comp_config* config) {
  memset(config, 0, sizeof(*config));
  config->comp_id = comp_id;
  config->name = widget->name;
  config->core = token_value(widget, AC_SOF_TKN_COMP_CORE_ID, 0);
  config->type = comp_type_for_widget(widget);
  config->periods_sink = (uint16_t)token_value(widget, AC_SOF_TKN_COMP_PERIOD_SINK_COUNT, 2);
  config->periods_source = (uint16_t)token_value(widget, AC_SOF_TKN_COMP_PERIOD_SOURCE_COUNT, 2);
  config->has_uuid = token_uuid(widget, config->uuid) ? true : false;

  switch (config->type) {
    case SOF_COMP_HOST:
      config->host.stream_name = widget->stream_name;
      break;
    case SOF_COMP_DAI:
      config->dai.type = (uint32_t)dai_type_from_string(token_string(widget, AC_SOF_TKN_DAI_TYPE));
      config->dai.dai_index = token_value(widget, AC_SOF_TKN_DAI_INDEX, 0);
      break;
    case SOF_COMP_VOLUME:
      config->volume.channels = 2;
      config->volume.min_value = 0;
      config->volume.max_value = 0xffffffffu;
      break;
    case SOF_COMP_SRC:
      config->src.source_rate = token_value(widget, AC_SOF_TKN_SRC_RATE_IN, 0);
      config->src.sink_rate = token_value(widget, AC_SOF_TKN_SRC_RATE_OUT, 0);
      break;
    case SOF_COMP_ASRC:
      config->asrc.source_rate = token_value(widget, AC_SOF_TKN_ASRC_RATE_IN, 0);
      config->asrc.sink_rate = token_value(widget, AC_SOF_TKN_ASRC_RATE_OUT, 0);
      config->asrc.asynchronous_mode = token_value(widget, AC_SOF_TKN_ASRC_ASYNCHRONOUS_MODE, 0);
      config->asrc.operation_mode = token_value(widget, AC_SOF_TKN_ASRC_OPERATION_MODE, 0);
      break;
    default:
      config->def.size = 0;
      config->def.type = 0;
      config->def.data = NULL;
      break;
  }
}

static int collect_route_ids(const ac_topology_t* topology,
                             const ac_widget_t* widget,
                             const uint16_t* comp_ids,
                             int source_side,
                             uint16_t ids[],
                             uint16_t* count) {
  size_t i;
  *count = 0;
  for (i = 0; i < topology->route_count; ++i) {
    const ac_route_t* route = &topology->routes[i];
    const char* other_name = NULL;
    int other_index;
    if (source_side) {
      if (strcmp(route->sink, widget->name) == 0) other_name = route->source;
    } else {
      if (strcmp(route->source, widget->name) == 0) other_name = route->sink;
    }
    if (other_name == NULL || other_name[0] == '\0') continue;
    other_index = find_widget_index_by_name(topology, other_name);
    if (other_index < 0 || comp_ids[other_index] == 0) continue;
    if (*count >= 8u) return -1;
    ids[*count] = comp_ids[other_index];
    (*count)++;
  }
  return 0;
}

static int install_one_pipeline(audio_controller_t* controller, const ac_pipeline_t* pipeline) {
  const ac_topology_t* topology = &controller->topology;
  const ac_widget_t* scheduler = find_scheduler(topology, pipeline);
  struct sof_mem_ops mem_ops;
  struct sof_pipe_config pipe_config;
  struct sof_pipe* pipe;
  uint16_t* comp_ids;
  uint16_t next_comp_id = 1;
  uint16_t sched_comp_id = 0;
  size_t i;
  int ret;

  if (scheduler == NULL) {
    ac_set_error(controller, "pipeline %s does not have a scheduler widget", pipeline->name);
    return -1;
  }

  comp_ids = (uint16_t*)controller->platform.alloc(controller->platform.user,
                                                  topology->widget_count * sizeof(uint16_t),
                                                  sizeof(uint16_t));
  if (comp_ids == NULL) {
    ac_set_error(controller, "out of memory while building install plan");
    return -1;
  }
  memset(comp_ids, 0, topology->widget_count * sizeof(uint16_t));

  for (i = 0; i < topology->widget_count; ++i) {
    const ac_widget_t* widget = &topology->widgets[i];
    if (strcmp(widget->pipeline_name, pipeline->name) != 0) continue;
    if (widget->id == AC_DAPM_SCHEDULER) {
      sched_comp_id = next_comp_id++;
      continue;
    }
    if (comp_type_for_widget(widget) != SOF_COMP_NONE) comp_ids[i] = next_comp_id++;
  }

  mem_ops.smalloc = sof_mem_alloc;
  mem_ops.sfree = sof_mem_free;
  g_mem_controller = controller;
  pipe = sof_pipeline_create(&mem_ops);
  if (pipe == NULL) {
    controller->platform.free(controller->platform.user, comp_ids);
    ac_set_error(controller, "sof_pipeline_create failed for %s", pipeline->name);
    return -1;
  }

  memset(&pipe_config, 0, sizeof(pipe_config));
  pipe_config.pipe_idx = pipeline->index + 1u;
  pipe_config.core = token_value(scheduler, AC_SOF_TKN_SCHED_CORE, 0);
  pipe_config.period = token_value(scheduler, AC_SOF_TKN_SCHED_PERIOD, 10000);
  pipe_config.priority = (uint8_t)token_value(scheduler, AC_SOF_TKN_SCHED_PRIORITY, 0);
  pipe_config.channel_max = 2;
  pipe_config.bytes_per_sample_max = 4;
  pipe_config.sample_rate_max = 48000;
  pipe_config.pipe_sched_id = sched_comp_id;
  pipe_config.last_sub_pipe = true;
  (void)sof_pipeline_config(pipe, PIPE_CFG_PIPE_INDEX, &pipe_config);
  (void)sof_pipeline_config(pipe, PIPE_CFG_PIPE_CORE_ID, &pipe_config);
  (void)sof_pipeline_config(pipe, PIPE_CFG_PERIOD, &pipe_config);
  (void)sof_pipeline_config(pipe, PIPE_CFG_PRIORITY, &pipe_config);
  (void)sof_pipeline_config(pipe, PIPE_CFG_CHANNEL_MAX, &pipe_config);
  (void)sof_pipeline_config(pipe, PIPE_CFG_BYTES_PER_SAMPLE_MAX, &pipe_config);
  (void)sof_pipeline_config(pipe, PIPE_CFG_SAMPLE_RATE_MAX, &pipe_config);
  (void)sof_pipeline_config(pipe, PIPE_CFG_PIPE_SCHED_ID, &pipe_config);
  (void)sof_pipeline_config(pipe, PIPE_CFG_LAST_SUB_PIPE, &pipe_config);

  for (i = 0; i < topology->widget_count; ++i) {
    const ac_widget_t* widget = &topology->widgets[i];
    struct sof_comp_config comp_config;
    uint16_t src_ids[8];
    uint16_t sink_ids[8];
    uint16_t src_count;
    uint16_t sink_count;
    if (comp_ids[i] == 0) continue;
    if (collect_route_ids(topology, widget, comp_ids, 1, src_ids, &src_count) != 0 ||
        collect_route_ids(topology, widget, comp_ids, 0, sink_ids, &sink_count) != 0) {
      (void)sof_pipeline_destroy(pipe);
      controller->platform.free(controller->platform.user, comp_ids);
      ac_set_error(controller, "too many routes for component %s", widget->name);
      return -1;
    }
    fill_comp_config(widget, comp_ids[i], &comp_config);
    ret = sof_pipeline_add_comp(pipe, &comp_config, src_ids, src_count, sink_ids, sink_count);
    if (ret < 0) {
      (void)sof_pipeline_destroy(pipe);
      controller->platform.free(controller->platform.user, comp_ids);
      ac_set_error(controller, "sof_pipeline_add_comp failed for %s: %d", widget->name, ret);
      return -1;
    }
  }

  ret = sof_pipeline_install(pipe);
  (void)sof_pipeline_destroy(pipe);
  controller->platform.free(controller->platform.user, comp_ids);
  if (ret < 0) {
    ac_set_error(controller, "sof_pipeline_install failed for %s: %d", pipeline->name, ret);
    return -1;
  }
  return 0;
}
#endif

int audio_controller_install_pipeline(audio_controller_t* controller, const char* id_or_name) {
#ifdef AUDIO_CONTROLLER_WITH_SOF_CLIENT
  size_t i;
  if (!sof_symbols_available()) {
    ac_set_error(controller, "real sof_client symbols are not linked");
    return -1;
  }
  if (id_or_name == NULL || id_or_name[0] == '\0') {
    ac_set_error(controller, "pipeline id/name is empty");
    return -1;
  }
  for (i = 0; i < controller->topology.pipeline_count; ++i) {
    char index_text[16];
    (void)snprintf(index_text, sizeof(index_text), "%u", controller->topology.pipelines[i].index + 1u);
    if (strcmp(controller->topology.pipelines[i].name, id_or_name) == 0 ||
        strcmp(index_text, id_or_name) == 0) {
      return install_one_pipeline(controller, &controller->topology.pipelines[i]);
    }
  }
  ac_set_error(controller, "pipeline not found: %s", id_or_name);
#else
  (void)id_or_name;
  ac_set_error(controller, "audio_controller was built without real sof_client support");
#endif
  return -1;
}

int audio_controller_install_all(audio_controller_t* controller) {
#ifdef AUDIO_CONTROLLER_WITH_SOF_CLIENT
  size_t i;
  if (!sof_symbols_available()) {
    ac_set_error(controller, "real sof_client symbols are not linked");
    return -1;
  }
  for (i = 0; i < controller->topology.pipeline_count; ++i) {
    if (install_one_pipeline(controller, &controller->topology.pipelines[i]) != 0) return -1;
  }
  return 0;
#else
  ac_set_error(controller, "audio_controller was built without real sof_client support");
  return -1;
#endif
}
