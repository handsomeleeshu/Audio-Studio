#include "topology_parser.h"

#include "topology_uapi.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_error(char* error, size_t error_size, const char* fmt, ...) {
  va_list args;
  if (error_size == 0) return;
  va_start(args, fmt);
  (void)vsnprintf(error, error_size, fmt, args);
  va_end(args);
  error[error_size - 1] = '\0';
}

static uint32_t read_u32(const void* ptr) {
  uint32_t value;
  memcpy(&value, ptr, sizeof(value));
  return value;
}

static void copy_name(char* dst, size_t dst_size, const char* src, size_t src_size) {
  size_t n = 0;
  if (dst_size == 0) return;
  while (n < src_size && n + 1 < dst_size && src[n] != '\0') {
    dst[n] = src[n];
    ++n;
  }
  dst[n] = '\0';
}

static void pipeline_prefix(char* dst, size_t dst_size, const char* name) {
  size_t i = 0;
  if (dst_size == 0) return;
  while (name[i] != '\0' && name[i] != '.' && i + 1 < dst_size) {
    dst[i] = name[i];
    ++i;
  }
  dst[i] = '\0';
  if (dst[0] == '\0') copy_name(dst, dst_size, name, strlen(name));
}

static void* ac_alloc(const ac_allocator_t* allocator, size_t size) {
  if (size == 0) size = 1;
  return allocator->alloc(allocator->user, size, sizeof(void*));
}

static void ac_free(const ac_allocator_t* allocator, void* ptr) {
  if (ptr != NULL) allocator->free(allocator->user, ptr);
}

static int reserve_items(void** items,
                         size_t* capacity,
                         size_t needed,
                         size_t item_size,
                         const ac_allocator_t* allocator) {
  void* next;
  size_t next_capacity;
  if (needed <= *capacity) return 0;
  next_capacity = *capacity == 0 ? 8u : *capacity;
  while (next_capacity < needed) next_capacity *= 2u;
  next = ac_alloc(allocator, next_capacity * item_size);
  if (next == NULL) return -1;
  if (*items != NULL) {
    memcpy(next, *items, (*capacity) * item_size);
    ac_free(allocator, *items);
  }
  memset((char*)next + (*capacity) * item_size, 0, (next_capacity - *capacity) * item_size);
  *items = next;
  *capacity = next_capacity;
  return 0;
}

static int append_token(ac_token_list_t* list, const ac_token_t* token, const ac_allocator_t* allocator) {
  if (reserve_items((void**)&list->items, &list->capacity, list->count + 1u, sizeof(list->items[0]), allocator) != 0) {
    return -1;
  }
  list->items[list->count++] = *token;
  return 0;
}

static int append_widget(ac_topology_t* topology, const ac_widget_t* widget, const ac_allocator_t* allocator) {
  static size_t capacity;
  if (topology->widget_count == 0) capacity = 0;
  if (reserve_items((void**)&topology->widgets, &capacity, topology->widget_count + 1u,
                    sizeof(topology->widgets[0]), allocator) != 0) {
    return -1;
  }
  topology->widgets[topology->widget_count++] = *widget;
  topology->summary.widgets = (uint32_t)topology->widget_count;
  return 0;
}

static int append_route(ac_topology_t* topology, const ac_route_t* route, const ac_allocator_t* allocator) {
  static size_t capacity;
  if (topology->route_count == 0) capacity = 0;
  if (reserve_items((void**)&topology->routes, &capacity, topology->route_count + 1u,
                    sizeof(topology->routes[0]), allocator) != 0) {
    return -1;
  }
  topology->routes[topology->route_count++] = *route;
  topology->summary.routes = (uint32_t)topology->route_count;
  return 0;
}

static int append_pcm(ac_topology_t* topology, const ac_pcm_t* pcm, const ac_allocator_t* allocator) {
  static size_t capacity;
  if (topology->pcm_count == 0) capacity = 0;
  if (reserve_items((void**)&topology->pcms, &capacity, topology->pcm_count + 1u,
                    sizeof(topology->pcms[0]), allocator) != 0) {
    return -1;
  }
  topology->pcms[topology->pcm_count++] = *pcm;
  topology->summary.pcms = (uint32_t)topology->pcm_count;
  return 0;
}

static int append_dai(ac_topology_t* topology, const ac_dai_t* dai, const ac_allocator_t* allocator) {
  static size_t capacity;
  if (topology->dai_count == 0) capacity = 0;
  if (reserve_items((void**)&topology->dais, &capacity, topology->dai_count + 1u,
                    sizeof(topology->dais[0]), allocator) != 0) {
    return -1;
  }
  topology->dais[topology->dai_count++] = *dai;
  topology->summary.dais = (uint32_t)topology->dai_count;
  return 0;
}

static int append_link(ac_topology_t* topology, const ac_link_t* link, const ac_allocator_t* allocator) {
  static size_t capacity;
  if (topology->link_count == 0) capacity = 0;
  if (reserve_items((void**)&topology->links, &capacity, topology->link_count + 1u,
                    sizeof(topology->links[0]), allocator) != 0) {
    return -1;
  }
  topology->links[topology->link_count++] = *link;
  topology->summary.links = (uint32_t)topology->link_count;
  return 0;
}

static int append_control(ac_topology_t* topology, const ac_control_t* control, const ac_allocator_t* allocator) {
  static size_t capacity;
  if (topology->control_count == 0) capacity = 0;
  if (reserve_items((void**)&topology->controls, &capacity, topology->control_count + 1u,
                    sizeof(topology->controls[0]), allocator) != 0) {
    return -1;
  }
  topology->controls[topology->control_count++] = *control;
  topology->summary.controls = (uint32_t)topology->control_count;
  return 0;
}

static ac_pipeline_t* find_pipeline(ac_topology_t* topology, const char* name) {
  size_t i;
  for (i = 0; i < topology->pipeline_count; ++i) {
    if (strcmp(topology->pipelines[i].name, name) == 0) return &topology->pipelines[i];
  }
  return NULL;
}

static ac_pipeline_t* append_pipeline(ac_topology_t* topology,
                                      const char* name,
                                      const ac_allocator_t* allocator) {
  static size_t capacity;
  ac_pipeline_t pipeline;
  ac_pipeline_t* found = find_pipeline(topology, name);
  if (found != NULL) return found;
  if (topology->pipeline_count == 0) capacity = 0;
  if (reserve_items((void**)&topology->pipelines, &capacity, topology->pipeline_count + 1u,
                    sizeof(topology->pipelines[0]), allocator) != 0) {
    return NULL;
  }
  memset(&pipeline, 0, sizeof(pipeline));
  copy_name(pipeline.name, sizeof(pipeline.name), name, strlen(name));
  pipeline.index = (uint32_t)topology->pipeline_count;
  topology->pipelines[topology->pipeline_count] = pipeline;
  topology->pipeline_count++;
  topology->summary.pipelines = (uint32_t)topology->pipeline_count;
  return &topology->pipelines[topology->pipeline_count - 1u];
}

static int parse_vendor_arrays(const uint8_t* data,
                               size_t size,
                               ac_token_list_t* tokens,
                               const ac_allocator_t* allocator,
                               char* error,
                               size_t error_size) {
  size_t offset = 0;
  while (offset < size) {
    const ac_tplg_vendor_array_t* array;
    size_t elem_size = 0;
    size_t i;
    if (size - offset < sizeof(*array)) {
      set_error(error, error_size, "truncated vendor tuple array");
      return -1;
    }
    array = (const ac_tplg_vendor_array_t*)(const void*)(data + offset);
    if (array->size < sizeof(*array) || array->size > size - offset) {
      set_error(error, error_size, "invalid vendor tuple array size");
      return -1;
    }
    if (array->type == AC_TPLG_TUPLE_TYPE_UUID) elem_size = sizeof(ac_tplg_vendor_uuid_elem_t);
    else if (array->type == AC_TPLG_TUPLE_TYPE_STRING) elem_size = sizeof(ac_tplg_vendor_string_elem_t);
    else elem_size = sizeof(ac_tplg_vendor_value_elem_t);
    if (sizeof(*array) + (size_t)array->num_elems * elem_size > array->size) {
      set_error(error, error_size, "invalid vendor tuple element count");
      return -1;
    }
    for (i = 0; i < array->num_elems; ++i) {
      ac_token_t token;
      const uint8_t* elem = data + offset + sizeof(*array) + i * elem_size;
      memset(&token, 0, sizeof(token));
      if (array->type == AC_TPLG_TUPLE_TYPE_UUID) {
        const ac_tplg_vendor_uuid_elem_t* uuid_elem = (const ac_tplg_vendor_uuid_elem_t*)(const void*)elem;
        token.token = uuid_elem->token;
        token.type = AC_TOKEN_UUID;
        memcpy(token.uuid, uuid_elem->uuid, sizeof(token.uuid));
      } else if (array->type == AC_TPLG_TUPLE_TYPE_STRING) {
        const ac_tplg_vendor_string_elem_t* string_elem = (const ac_tplg_vendor_string_elem_t*)(const void*)elem;
        token.token = string_elem->token;
        token.type = AC_TOKEN_STRING;
        copy_name(token.string, sizeof(token.string), string_elem->string, sizeof(string_elem->string));
      } else {
        const ac_tplg_vendor_value_elem_t* value_elem = (const ac_tplg_vendor_value_elem_t*)(const void*)elem;
        token.token = value_elem->token;
        token.type = AC_TOKEN_VALUE;
        token.value = value_elem->value;
      }
      if (append_token(tokens, &token, allocator) != 0) {
        set_error(error, error_size, "out of memory while storing vendor tuples");
        return -1;
      }
    }
    offset += array->size;
  }
  return 0;
}

static int parse_controls(ac_topology_t* topology,
                          const uint8_t* payload,
                          size_t payload_size,
                          uint32_t count,
                          uint32_t type,
                          const ac_allocator_t* allocator,
                          char* error,
                          size_t error_size) {
  size_t offset = 0;
  uint32_t i;
  for (i = 0; i < count; ++i) {
    ac_control_t control;
    uint32_t hdr_size;
    uint32_t object_size;
    if (payload_size - offset < sizeof(ac_tplg_ctl_hdr_t)) {
      set_error(error, error_size, "truncated control block");
      return -1;
    }
    hdr_size = read_u32(payload + offset);
    if (hdr_size < sizeof(ac_tplg_ctl_hdr_t) || hdr_size + sizeof(uint32_t) > payload_size - offset) {
      set_error(error, error_size, "invalid control header size");
      return -1;
    }
    object_size = read_u32(payload + offset + hdr_size);
    if (object_size < hdr_size || object_size > payload_size - offset) {
      set_error(error, error_size, "invalid control object size");
      return -1;
    }
    memset(&control, 0, sizeof(control));
    control.type = type;
    copy_name(control.name, sizeof(control.name),
              (const char*)(const void*)(payload + offset + 8u), AC_TPLG_NAME_SIZE);
    if (append_control(topology, &control, allocator) != 0) {
      set_error(error, error_size, "out of memory while storing controls");
      return -1;
    }
    offset += object_size;
  }
  return 0;
}

static int parse_widgets(ac_topology_t* topology,
                         const uint8_t* payload,
                         size_t payload_size,
                         uint32_t count,
                         uint32_t block_index,
                         const ac_allocator_t* allocator,
                         char* error,
                         size_t error_size) {
  size_t offset = 0;
  uint32_t i;
  for (i = 0; i < count; ++i) {
    const ac_tplg_dapm_widget_t* raw;
    ac_widget_t widget;
    ac_pipeline_t* pipeline;
    if (payload_size - offset < sizeof(*raw)) {
      set_error(error, error_size, "truncated widget block");
      return -1;
    }
    raw = (const ac_tplg_dapm_widget_t*)(const void*)(payload + offset);
    if (raw->size < sizeof(*raw) || raw->size > payload_size - offset) {
      set_error(error, error_size, "invalid widget size");
      return -1;
    }
    memset(&widget, 0, sizeof(widget));
    widget.id = raw->id;
    widget.block_index = block_index;
    widget.num_kcontrols = raw->num_kcontrols;
    copy_name(widget.name, sizeof(widget.name), raw->name, sizeof(raw->name));
    copy_name(widget.stream_name, sizeof(widget.stream_name), raw->sname, sizeof(raw->sname));
    pipeline_prefix(widget.pipeline_name, sizeof(widget.pipeline_name), widget.name);
    if (raw->priv_size > 0u) {
      const size_t private_offset = raw->size;
      if ((size_t)raw->priv_size > payload_size - offset - private_offset) {
        set_error(error, error_size, "invalid widget private data size");
        return -1;
      }
      topology->summary.private_blocks++;
      if (parse_vendor_arrays(payload + offset + private_offset, raw->priv_size, &widget.tokens,
                              allocator, error, error_size) != 0) {
        return -1;
      }
    }
    pipeline = append_pipeline(topology, widget.pipeline_name, allocator);
    if (pipeline == NULL) {
      set_error(error, error_size, "out of memory while storing pipelines");
      return -1;
    }
    pipeline->widget_count++;
    if (widget.id == AC_DAPM_SCHEDULER || strstr(widget.name, ".SCHEDULE") != NULL) pipeline->has_scheduler = 1;
    if (append_widget(topology, &widget, allocator) != 0) {
      set_error(error, error_size, "out of memory while storing widgets");
      return -1;
    }
    offset += (size_t)raw->size + raw->priv_size;
    {
      uint32_t control_index;
      for (control_index = 0; control_index < raw->num_kcontrols; ++control_index) {
        uint32_t hdr_size;
        uint32_t object_size;
        if (payload_size - offset < sizeof(ac_tplg_ctl_hdr_t)) {
          set_error(error, error_size, "truncated widget control");
          return -1;
        }
        hdr_size = read_u32(payload + offset);
        if (hdr_size < sizeof(ac_tplg_ctl_hdr_t) || hdr_size + sizeof(uint32_t) > payload_size - offset) {
          set_error(error, error_size, "invalid widget control header size");
          return -1;
        }
        object_size = read_u32(payload + offset + hdr_size);
        if (object_size < hdr_size || object_size > payload_size - offset) {
          set_error(error, error_size, "invalid widget control object size");
          return -1;
        }
        {
          ac_control_t control;
          memset(&control, 0, sizeof(control));
          control.type = read_u32(payload + offset + sizeof(uint32_t));
          copy_name(control.name, sizeof(control.name),
                    (const char*)(const void*)(payload + offset + 8u), AC_TPLG_NAME_SIZE);
          if (append_control(topology, &control, allocator) != 0) {
            set_error(error, error_size, "out of memory while storing widget controls");
            return -1;
          }
        }
        offset += object_size;
      }
    }
  }
  return 0;
}

static int parse_routes(ac_topology_t* topology,
                        const uint8_t* payload,
                        size_t payload_size,
                        uint32_t count,
                        const ac_allocator_t* allocator,
                        char* error,
                        size_t error_size) {
  size_t offset = 0;
  uint32_t i;
  for (i = 0; i < count; ++i) {
    const ac_tplg_dapm_graph_elem_t* raw;
    ac_route_t route;
    if (payload_size - offset < sizeof(*raw)) {
      set_error(error, error_size, "truncated route block");
      return -1;
    }
    raw = (const ac_tplg_dapm_graph_elem_t*)(const void*)(payload + offset);
    memset(&route, 0, sizeof(route));
    copy_name(route.sink, sizeof(route.sink), raw->sink, sizeof(raw->sink));
    copy_name(route.control, sizeof(route.control), raw->control, sizeof(raw->control));
    copy_name(route.source, sizeof(route.source), raw->source, sizeof(raw->source));
    if (append_route(topology, &route, allocator) != 0) {
      set_error(error, error_size, "out of memory while storing routes");
      return -1;
    }
    offset += sizeof(*raw);
  }
  return 0;
}

static int parse_pcms(ac_topology_t* topology,
                      const uint8_t* payload,
                      size_t payload_size,
                      uint32_t count,
                      const ac_allocator_t* allocator,
                      char* error,
                      size_t error_size) {
  size_t offset = 0;
  uint32_t i;
  for (i = 0; i < count; ++i) {
    const ac_tplg_pcm_t* raw;
    ac_pcm_t pcm;
    if (payload_size - offset < sizeof(*raw)) {
      set_error(error, error_size, "truncated PCM block");
      return -1;
    }
    raw = (const ac_tplg_pcm_t*)(const void*)(payload + offset);
    if (raw->size < sizeof(*raw) || raw->size > payload_size - offset ||
        raw->priv_size > payload_size - offset - raw->size) {
      set_error(error, error_size, "invalid PCM size");
      return -1;
    }
    memset(&pcm, 0, sizeof(pcm));
    copy_name(pcm.name, sizeof(pcm.name), raw->pcm_name, sizeof(raw->pcm_name));
    copy_name(pcm.dai_name, sizeof(pcm.dai_name), raw->dai_name, sizeof(raw->dai_name));
    pcm.id = raw->pcm_id;
    pcm.dai_id = raw->dai_id;
    pcm.playback = raw->playback;
    pcm.capture = raw->capture;
    if (raw->priv_size > 0u) topology->summary.private_blocks++;
    if (append_pcm(topology, &pcm, allocator) != 0) {
      set_error(error, error_size, "out of memory while storing PCMs");
      return -1;
    }
    offset += (size_t)raw->size + raw->priv_size;
  }
  return 0;
}

static int parse_dais(ac_topology_t* topology,
                      const uint8_t* payload,
                      size_t payload_size,
                      uint32_t count,
                      const ac_allocator_t* allocator,
                      char* error,
                      size_t error_size) {
  size_t offset = 0;
  uint32_t i;
  for (i = 0; i < count; ++i) {
    const ac_tplg_dai_t* raw;
    ac_dai_t dai;
    if (payload_size - offset < sizeof(*raw)) {
      set_error(error, error_size, "truncated DAI block");
      return -1;
    }
    raw = (const ac_tplg_dai_t*)(const void*)(payload + offset);
    if (raw->size < sizeof(*raw) || raw->size > payload_size - offset ||
        raw->priv_size > payload_size - offset - raw->size) {
      set_error(error, error_size, "invalid DAI size");
      return -1;
    }
    memset(&dai, 0, sizeof(dai));
    copy_name(dai.name, sizeof(dai.name), raw->dai_name, sizeof(raw->dai_name));
    dai.id = raw->dai_id;
    dai.playback = raw->playback;
    dai.capture = raw->capture;
    if (raw->priv_size > 0u) topology->summary.private_blocks++;
    if (append_dai(topology, &dai, allocator) != 0) {
      set_error(error, error_size, "out of memory while storing DAIs");
      return -1;
    }
    offset += (size_t)raw->size + raw->priv_size;
  }
  return 0;
}

static int parse_links(ac_topology_t* topology,
                       const uint8_t* payload,
                       size_t payload_size,
                       uint32_t count,
                       const ac_allocator_t* allocator,
                       char* error,
                       size_t error_size) {
  size_t offset = 0;
  uint32_t i;
  for (i = 0; i < count; ++i) {
    const ac_tplg_link_config_t* raw;
    ac_link_t link;
    if (payload_size - offset < sizeof(*raw)) {
      set_error(error, error_size, "truncated link block");
      return -1;
    }
    raw = (const ac_tplg_link_config_t*)(const void*)(payload + offset);
    if (raw->size < sizeof(*raw) || raw->size > payload_size - offset ||
        raw->priv_size > payload_size - offset - raw->size) {
      set_error(error, error_size, "invalid link size");
      return -1;
    }
    memset(&link, 0, sizeof(link));
    copy_name(link.name, sizeof(link.name), raw->name, sizeof(raw->name));
    copy_name(link.stream_name, sizeof(link.stream_name), raw->stream_name, sizeof(raw->stream_name));
    link.id = raw->id;
    link.num_hw_configs = raw->num_hw_configs;
    link.default_hw_config_id = raw->default_hw_config_id;
    if (raw->priv_size > 0u) topology->summary.private_blocks++;
    if (append_link(topology, &link, allocator) != 0) {
      set_error(error, error_size, "out of memory while storing links");
      return -1;
    }
    offset += (size_t)raw->size + raw->priv_size;
  }
  return 0;
}

static int parse_manifest(ac_topology_t* topology,
                          const uint8_t* payload,
                          size_t payload_size,
                          char* error,
                          size_t error_size) {
  const ac_tplg_manifest_t* raw;
  if (payload_size < sizeof(*raw)) {
    set_error(error, error_size, "truncated manifest block");
    return -1;
  }
  raw = (const ac_tplg_manifest_t*)(const void*)payload;
  if (raw->size < sizeof(*raw) || raw->size > payload_size ||
      raw->priv_size > payload_size - raw->size) {
    set_error(error, error_size, "invalid manifest size");
    return -1;
  }
  topology->summary.manifests++;
  if (raw->priv_size > 0u) topology->summary.private_blocks++;
  return 0;
}

static void count_pipeline_routes(ac_topology_t* topology) {
  size_t i;
  for (i = 0; i < topology->route_count; ++i) {
    char prefix[AC_MAX_NAME];
    ac_pipeline_t* pipeline;
    pipeline_prefix(prefix, sizeof(prefix), topology->routes[i].sink);
    pipeline = find_pipeline(topology, prefix);
    if (pipeline != NULL) pipeline->route_count++;
  }
}

void ac_topology_init(ac_topology_t* topology) {
  memset(topology, 0, sizeof(*topology));
}

void ac_topology_clear(ac_topology_t* topology, const ac_allocator_t* allocator) {
  size_t i;
  if (topology == NULL || allocator == NULL) return;
  for (i = 0; i < topology->widget_count; ++i) {
    ac_free(allocator, topology->widgets[i].tokens.items);
  }
  ac_free(allocator, topology->pcms);
  ac_free(allocator, topology->dais);
  ac_free(allocator, topology->links);
  ac_free(allocator, topology->widgets);
  ac_free(allocator, topology->routes);
  ac_free(allocator, topology->controls);
  ac_free(allocator, topology->pipelines);
  ac_topology_init(topology);
}

int ac_parse_topology(const void* data,
                      size_t size,
                      const ac_allocator_t* allocator,
                      ac_topology_t* topology,
                      char* error,
                      size_t error_size) {
  const uint8_t* bytes = (const uint8_t*)data;
  size_t offset = 0;
  ac_topology_clear(topology, allocator);
  while (offset < size) {
    const ac_tplg_hdr_t* hdr;
    const uint8_t* payload;
    if (size - offset < sizeof(*hdr)) {
      set_error(error, error_size, "truncated topology header");
      return -1;
    }
    hdr = (const ac_tplg_hdr_t*)(const void*)(bytes + offset);
    if (hdr->magic != AC_TPLG_MAGIC) {
      set_error(error, error_size, "invalid topology magic at offset %lu", (unsigned long)offset);
      return -1;
    }
    if (hdr->abi < AC_TPLG_ABI_VERSION_MIN || hdr->abi > AC_TPLG_ABI_VERSION) {
      set_error(error, error_size, "unsupported topology ABI %u", hdr->abi);
      return -1;
    }
    if (hdr->size < sizeof(*hdr) || hdr->size > size - offset) {
      set_error(error, error_size, "invalid topology header size");
      return -1;
    }
    if (hdr->payload_size > size - offset - hdr->size) {
      set_error(error, error_size, "invalid topology payload size");
      return -1;
    }
    topology->summary.abi = hdr->abi;
    payload = bytes + offset + hdr->size;
    switch (hdr->type) {
      case AC_TPLG_TYPE_MANIFEST:
        if (parse_manifest(topology, payload, hdr->payload_size, error, error_size) != 0) return -1;
        break;
      case AC_TPLG_TYPE_MIXER:
      case AC_TPLG_TYPE_BYTES:
      case AC_TPLG_TYPE_ENUM:
        if (parse_controls(topology, payload, hdr->payload_size, hdr->count, hdr->type,
                           allocator, error, error_size) != 0) {
          return -1;
        }
        break;
      case AC_TPLG_TYPE_DAPM_WIDGET:
        if (parse_widgets(topology, payload, hdr->payload_size, hdr->count, hdr->index,
                          allocator, error, error_size) != 0) {
          return -1;
        }
        break;
      case AC_TPLG_TYPE_DAPM_GRAPH:
        if (parse_routes(topology, payload, hdr->payload_size, hdr->count,
                         allocator, error, error_size) != 0) {
          return -1;
        }
        break;
      case AC_TPLG_TYPE_PCM:
        if (parse_pcms(topology, payload, hdr->payload_size, hdr->count,
                       allocator, error, error_size) != 0) {
          return -1;
        }
        break;
      case AC_TPLG_TYPE_DAI:
        if (parse_dais(topology, payload, hdr->payload_size, hdr->count,
                       allocator, error, error_size) != 0) {
          return -1;
        }
        break;
      case AC_TPLG_TYPE_DAI_LINK:
      case AC_TPLG_TYPE_BACKEND_LINK:
        if (parse_links(topology, payload, hdr->payload_size, hdr->count,
                        allocator, error, error_size) != 0) {
          return -1;
        }
        break;
      case AC_TPLG_TYPE_PDATA:
        topology->summary.private_blocks++;
        break;
      default:
        break;
    }
    offset += hdr->size + hdr->payload_size;
  }
  count_pipeline_routes(topology);
  return 0;
}

static int appendf(char** cursor, size_t* remaining, const char* fmt, ...) {
  va_list args;
  int written;
  if (*remaining == 0) return -1;
  va_start(args, fmt);
  written = vsnprintf(*cursor, *remaining, fmt, args);
  va_end(args);
  if (written < 0 || (size_t)written >= *remaining) {
    *remaining = 0;
    return -1;
  }
  *cursor += written;
  *remaining -= (size_t)written;
  return 0;
}

int ac_topology_format_list(const ac_topology_t* topology, char* buffer, size_t buffer_size) {
  char* cursor = buffer;
  size_t remaining = buffer_size;
  size_t i;
  if (buffer == NULL || buffer_size == 0) return -1;
  buffer[0] = '\0';
  if (appendf(&cursor, &remaining, "topology: abi %u\n", topology->summary.abi) != 0) return -1;
  if (appendf(&cursor, &remaining, "pipelines: %u\n", topology->summary.pipelines) != 0) return -1;
  for (i = 0; i < topology->pipeline_count; ++i) {
    if (appendf(&cursor, &remaining, "  - %s widgets:%u routes:%u scheduler:%s\n",
                topology->pipelines[i].name,
                topology->pipelines[i].widget_count,
                topology->pipelines[i].route_count,
                topology->pipelines[i].has_scheduler ? "yes" : "no") != 0) {
      return -1;
    }
  }
  if (appendf(&cursor, &remaining, "pcms: %u\n", topology->summary.pcms) != 0) return -1;
  for (i = 0; i < topology->pcm_count; ++i) {
    if (appendf(&cursor, &remaining, "  - %s id:%u playback:%u capture:%u\n",
                topology->pcms[i].name, topology->pcms[i].id,
                topology->pcms[i].playback, topology->pcms[i].capture) != 0) {
      return -1;
    }
  }
  if (appendf(&cursor, &remaining, "dais: %u\n", topology->summary.dais) != 0) return -1;
  for (i = 0; i < topology->dai_count; ++i) {
    if (appendf(&cursor, &remaining, "  - %s id:%u playback:%u capture:%u\n",
                topology->dais[i].name, topology->dais[i].id,
                topology->dais[i].playback, topology->dais[i].capture) != 0) {
      return -1;
    }
  }
  if (appendf(&cursor, &remaining, "links: %u\n", topology->summary.links) != 0) return -1;
  if (appendf(&cursor, &remaining, "widgets: %u\n", topology->summary.widgets) != 0) return -1;
  for (i = 0; i < topology->widget_count; ++i) {
    if (appendf(&cursor, &remaining, "  - %s type:%u tokens:%lu\n",
                topology->widgets[i].name, topology->widgets[i].id,
                (unsigned long)topology->widgets[i].tokens.count) != 0) {
      return -1;
    }
  }
  if (appendf(&cursor, &remaining, "routes: %u\n", topology->summary.routes) != 0) return -1;
  if (appendf(&cursor, &remaining, "controls: %u\n", topology->summary.controls) != 0) return -1;
  if (appendf(&cursor, &remaining, "private_blocks: %u\n", topology->summary.private_blocks) != 0) return -1;
  return 0;
}

const ac_token_t* ac_find_token(const ac_token_list_t* tokens, uint32_t token) {
  size_t i;
  for (i = 0; i < tokens->count; ++i) {
    if (tokens->items[i].token == token) return &tokens->items[i];
  }
  return NULL;
}
