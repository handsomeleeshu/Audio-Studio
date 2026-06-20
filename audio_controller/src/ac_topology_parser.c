#include "ac_topology_parser.h"

#include "ac_topology_uapi.h"

#include <string.h>

static void ac_parser_copy(char* dst, size_t dst_size,
                           const char* src, size_t src_size)
{
    size_t i;

    if (!dst || dst_size == 0u)
        return;

    if (!src) {
        dst[0] = '\0';
        return;
    }

    i = 0u;
    while (i < src_size && i + 1u < dst_size && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void ac_parser_append(char* dst, size_t dst_size, const char* src)
{
    size_t len;

    if (!dst || dst_size == 0u)
        return;

    len = strlen(dst);
    if (len >= dst_size)
        return;

    ac_parser_copy(dst + len, dst_size - len, src, strlen(src));
}

static void ac_parser_append_uint(char* dst, size_t dst_size, uint32_t value)
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

    ac_parser_append(dst, dst_size, &tmp[pos]);
}

static void ac_parser_set_error(char* error, size_t error_size,
                                const char* message)
{
    ac_parser_copy(error, error_size, message, strlen(message));
}

static void ac_parser_set_error_uint(char* error, size_t error_size,
                                     const char* message, uint32_t value)
{
    if (!error || error_size == 0u)
        return;

    error[0] = '\0';
    ac_parser_append(error, error_size, message);
    ac_parser_append_uint(error, error_size, value);
}

static uint32_t ac_parser_read_u32(const void* ptr)
{
    uint32_t value;

    memcpy(&value, ptr, sizeof(value));
    return value;
}

static void* ac_parser_alloc(const ac_allocator_t* allocator, size_t size)
{
    if (size == 0u)
        size = 1u;

    return allocator->alloc(allocator->user, size, sizeof(void*));
}

static void ac_parser_free(const ac_allocator_t* allocator, void* ptr)
{
    if (ptr)
        allocator->free(allocator->user, ptr);
}

static void ac_control_release(ac_control_t* control,
                               const ac_allocator_t* allocator)
{
    if (!control || !allocator)
        return;

    ac_parser_free(allocator, control->payload);
    control->payload = 0;
    control->payload_size = 0u;
    control->payload_type = 0u;
}

static void ac_control_list_release(ac_control_list_t* list,
                                    const ac_allocator_t* allocator)
{
    size_t i;

    if (!list || !allocator)
        return;

    for (i = 0u; i < list->count; i++)
        ac_control_release(&list->items[i], allocator);

    ac_parser_free(allocator, list->items);
    list->items = 0;
    list->count = 0u;
    list->capacity = 0u;
}

static void ac_widget_release(ac_widget_t* widget,
                              const ac_allocator_t* allocator)
{
    if (!widget || !allocator)
        return;

    ac_parser_free(allocator, widget->tokens.items);
    widget->tokens.items = 0;
    widget->tokens.count = 0u;
    widget->tokens.capacity = 0u;
    ac_control_list_release(&widget->controls, allocator);
}

static int ac_parser_reserve(void** items,
                             size_t* capacity,
                             size_t needed,
                             size_t item_size,
                             const ac_allocator_t* allocator)
{
    void* next;
    size_t next_capacity;
    size_t old_size;
    size_t new_size;

    if (needed <= *capacity)
        return 0;

    next_capacity = *capacity == 0u ? 8u : *capacity;
    while (next_capacity < needed)
        next_capacity *= 2u;

    new_size = next_capacity * item_size;
    next = ac_parser_alloc(allocator, new_size);
    if (!next)
        return -1;

    old_size = *capacity * item_size;
    if (*items) {
        memcpy(next, *items, old_size);
        ac_parser_free(allocator, *items);
    }
    memset((char*)next + old_size, 0, new_size - old_size);

    *items = next;
    *capacity = next_capacity;
    return 0;
}

static int ac_append_token(ac_token_list_t* list,
                           const ac_token_t* token,
                           const ac_allocator_t* allocator)
{
    int ret;

    ret = ac_parser_reserve((void**)&list->items, &list->capacity,
                            list->count + 1u, sizeof(list->items[0]),
                            allocator);
    if (ret != 0)
        return -1;

    list->items[list->count] = *token;
    list->count++;
    return 0;
}

static int ac_append_widget_control(ac_control_list_t* list,
                                    const ac_control_t* control,
                                    const ac_allocator_t* allocator)
{
    int ret;

    ret = ac_parser_reserve((void**)&list->items, &list->capacity,
                            list->count + 1u, sizeof(list->items[0]),
                            allocator);
    if (ret != 0)
        return -1;

    list->items[list->count] = *control;
    list->count++;
    return 0;
}

static ac_pipeline_t* ac_find_pipeline_by_id(ac_topology_t* topology,
                                             uint32_t id)
{
    size_t i;

    for (i = 0u; i < topology->pipeline_count; i++) {
        if (topology->pipelines[i].id == id)
            return &topology->pipelines[i];
    }

    return 0;
}

static void ac_pipeline_default_name(ac_pipeline_t* pipeline)
{
    pipeline->name[0] = '\0';
    ac_parser_append(pipeline->name, sizeof(pipeline->name), "pipeline.");
    ac_parser_append_uint(pipeline->name, sizeof(pipeline->name),
                          pipeline->id);
}

static ac_pipeline_t* ac_append_pipeline(ac_topology_t* topology,
                                         uint32_t id,
                                         const ac_allocator_t* allocator)
{
    ac_pipeline_t* pipeline;
    int ret;

    pipeline = ac_find_pipeline_by_id(topology, id);
    if (pipeline)
        return pipeline;

    ret = ac_parser_reserve((void**)&topology->pipelines,
                            &topology->pipeline_capacity,
                            topology->pipeline_count + 1u,
                            sizeof(topology->pipelines[0]), allocator);
    if (ret != 0)
        return 0;

    pipeline = &topology->pipelines[topology->pipeline_count];
    memset(pipeline, 0, sizeof(*pipeline));
    pipeline->id = id;
    pipeline->index = (uint32_t)topology->pipeline_count;
    pipeline->max_sample_rate = 48000u;
    pipeline->max_channels = 2u;
    pipeline->max_bytes_per_sample = 2u;
    ac_pipeline_default_name(pipeline);

    topology->pipeline_count++;
    topology->summary.pipelines = (uint32_t)topology->pipeline_count;
    return pipeline;
}

static int ac_append_widget(ac_topology_t* topology,
                            const ac_widget_t* widget,
                            const ac_allocator_t* allocator)
{
    int ret;

    ret = ac_parser_reserve((void**)&topology->widgets,
                            &topology->widget_capacity,
                            topology->widget_count + 1u,
                            sizeof(topology->widgets[0]), allocator);
    if (ret != 0)
        return -1;

    topology->widgets[topology->widget_count] = *widget;
    topology->widget_count++;
    topology->summary.widgets = (uint32_t)topology->widget_count;
    return 0;
}

static int ac_append_route(ac_topology_t* topology,
                           const ac_route_t* route,
                           const ac_allocator_t* allocator)
{
    int ret;

    ret = ac_parser_reserve((void**)&topology->routes,
                            &topology->route_capacity,
                            topology->route_count + 1u,
                            sizeof(topology->routes[0]), allocator);
    if (ret != 0)
        return -1;

    topology->routes[topology->route_count] = *route;
    topology->route_count++;
    topology->summary.routes = (uint32_t)topology->route_count;
    return 0;
}

static int ac_append_pcm(ac_topology_t* topology,
                         const ac_pcm_t* pcm,
                         const ac_allocator_t* allocator)
{
    int ret;

    ret = ac_parser_reserve((void**)&topology->pcms,
                            &topology->pcm_capacity,
                            topology->pcm_count + 1u,
                            sizeof(topology->pcms[0]), allocator);
    if (ret != 0)
        return -1;

    topology->pcms[topology->pcm_count] = *pcm;
    topology->pcm_count++;
    topology->summary.pcms = (uint32_t)topology->pcm_count;
    return 0;
}

static int ac_append_dai(ac_topology_t* topology,
                         const ac_dai_t* dai,
                         const ac_allocator_t* allocator)
{
    int ret;

    ret = ac_parser_reserve((void**)&topology->dais,
                            &topology->dai_capacity,
                            topology->dai_count + 1u,
                            sizeof(topology->dais[0]), allocator);
    if (ret != 0)
        return -1;

    topology->dais[topology->dai_count] = *dai;
    topology->dai_count++;
    topology->summary.dais = (uint32_t)topology->dai_count;
    return 0;
}

static int ac_append_link(ac_topology_t* topology,
                          const ac_link_t* link,
                          const ac_allocator_t* allocator)
{
    int ret;

    ret = ac_parser_reserve((void**)&topology->links,
                            &topology->link_capacity,
                            topology->link_count + 1u,
                            sizeof(topology->links[0]), allocator);
    if (ret != 0)
        return -1;

    topology->links[topology->link_count] = *link;
    topology->link_count++;
    topology->summary.links = (uint32_t)topology->link_count;
    return 0;
}

static int ac_append_control(ac_topology_t* topology,
                             const ac_control_t* control,
                             const ac_allocator_t* allocator)
{
    int ret;

    ret = ac_parser_reserve((void**)&topology->controls,
                            &topology->control_capacity,
                            topology->control_count + 1u,
                            sizeof(topology->controls[0]), allocator);
    if (ret != 0)
        return -1;

    topology->controls[topology->control_count] = *control;
    topology->control_count++;
    topology->summary.controls = (uint32_t)topology->control_count;
    return 0;
}

static int ac_parse_vendor_arrays(const uint8_t* data,
                                  size_t size,
                                  ac_token_list_t* tokens,
                                  const ac_allocator_t* allocator,
                                  char* error,
                                  size_t error_size)
{
    size_t offset;
    const ac_tplg_vendor_array_t* array;
    size_t elem_size;
    size_t i;
    const uint8_t* elem;
    ac_token_t token;
    const ac_tplg_vendor_uuid_elem_t* uuid_elem;
    const ac_tplg_vendor_string_elem_t* string_elem;
    const ac_tplg_vendor_value_elem_t* value_elem;

    offset = 0u;
    while (offset < size) {
        if (size - offset < sizeof(*array)) {
            ac_parser_set_error(error, error_size,
                                "truncated vendor tuple array");
            return -1;
        }

        array = (const ac_tplg_vendor_array_t*)(const void*)(data + offset);
        if (array->size < sizeof(*array) || array->size > size - offset) {
            ac_parser_set_error(error, error_size,
                                "invalid vendor tuple array size");
            return -1;
        }

        if (array->type == AC_TPLG_TUPLE_TYPE_UUID)
            elem_size = sizeof(ac_tplg_vendor_uuid_elem_t);
        else if (array->type == AC_TPLG_TUPLE_TYPE_STRING)
            elem_size = sizeof(ac_tplg_vendor_string_elem_t);
        else
            elem_size = sizeof(ac_tplg_vendor_value_elem_t);

        if (sizeof(*array) + (size_t)array->num_elems * elem_size >
            array->size) {
            ac_parser_set_error(error, error_size,
                                "invalid vendor tuple element count");
            return -1;
        }

        for (i = 0u; i < array->num_elems; i++) {
            elem = data + offset + sizeof(*array) + i * elem_size;
            memset(&token, 0, sizeof(token));

            if (array->type == AC_TPLG_TUPLE_TYPE_UUID) {
                uuid_elem =
                    (const ac_tplg_vendor_uuid_elem_t*)(const void*)elem;
                token.token = uuid_elem->token;
                token.type = AC_TOKEN_UUID;
                memcpy(token.uuid, uuid_elem->uuid, sizeof(token.uuid));
            } else if (array->type == AC_TPLG_TUPLE_TYPE_STRING) {
                string_elem =
                    (const ac_tplg_vendor_string_elem_t*)(const void*)elem;
                token.token = string_elem->token;
                token.type = AC_TOKEN_STRING;
                ac_parser_copy(token.string, sizeof(token.string),
                               string_elem->string,
                               sizeof(string_elem->string));
            } else {
                value_elem =
                    (const ac_tplg_vendor_value_elem_t*)(const void*)elem;
                token.token = value_elem->token;
                token.type = AC_TOKEN_VALUE;
                token.value = value_elem->value;
            }

            if (ac_append_token(tokens, &token, allocator) != 0) {
                ac_parser_set_error(error, error_size,
                                    "out of memory while storing tuples");
                return -1;
            }
        }

        offset += array->size;
    }

    return 0;
}

static int ac_copy_control_payload(ac_control_t* control,
                                   const uint8_t* payload,
                                   uint32_t payload_size,
                                   const ac_allocator_t* allocator)
{
    if (payload_size == 0u)
        return 0;

    control->payload = (uint8_t*)ac_parser_alloc(allocator, payload_size);
    if (!control->payload)
        return -1;

    memcpy(control->payload, payload, payload_size);
    control->payload_size = payload_size;
    return 0;
}

static int ac_parse_control(const uint8_t* payload,
                            size_t payload_size,
                            size_t* offset,
                            ac_control_t* control,
                            const ac_allocator_t* allocator,
                            int keep_payload)
{
    uint32_t hdr_size;
    uint32_t object_size;
    uint32_t private_size;
    const ac_tplg_ctl_hdr_t* hdr;
    const ac_tplg_private_t* private_header;
    const ac_tplg_bytes_control_t* bytes_control;
    const uint8_t* private_payload;

    if (payload_size - *offset < sizeof(ac_tplg_ctl_hdr_t))
        return -1;

    hdr = (const ac_tplg_ctl_hdr_t*)(const void*)(payload + *offset);
    hdr_size = hdr->size;
    if (hdr_size < sizeof(ac_tplg_ctl_hdr_t) ||
        hdr_size + sizeof(uint32_t) > payload_size - *offset)
        return -1;

    object_size = ac_parser_read_u32(payload + *offset + hdr_size);
    if (object_size < hdr_size || object_size > payload_size - *offset)
        return -1;
    if (object_size < sizeof(ac_tplg_private_t))
        return -1;

    private_header =
        (const ac_tplg_private_t*)(const void*)(payload + *offset +
                                               object_size -
                                               sizeof(*private_header));
    private_size = private_header->size;
    if ((size_t)object_size + private_size > payload_size - *offset)
        return -1;

    memset(control, 0, sizeof(*control));
    control->type = hdr->type;
    ac_parser_copy(control->name, sizeof(control->name), hdr->name,
                   sizeof(hdr->name));

    if (keep_payload && hdr->type == AC_TPLG_TYPE_BYTES &&
        private_size > 0u) {
        if (object_size < sizeof(*bytes_control))
            return -1;
        bytes_control =
            (const ac_tplg_bytes_control_t*)(const void*)(payload + *offset);
        private_payload = payload + *offset + object_size;
        control->payload_type = bytes_control->hdr.ops.info;
        if (ac_copy_control_payload(control, private_payload, private_size,
                                    allocator) != 0)
            return -1;
    }

    *offset += (size_t)object_size + private_size;

    return 0;
}

static int ac_parse_controls(ac_topology_t* topology,
                             const uint8_t* payload,
                             size_t payload_size,
                             uint32_t count,
                             uint32_t type,
                             const ac_allocator_t* allocator,
                             char* error,
                             size_t error_size)
{
    size_t offset;
    uint32_t i;
    ac_control_t control;

    offset = 0u;
    for (i = 0u; i < count; i++) {
        if (ac_parse_control(payload, payload_size, &offset, &control,
                             allocator, 0) != 0) {
            ac_parser_set_error(error, error_size,
                                "invalid control block");
            return -1;
        }
        control.type = type;
        if (ac_append_control(topology, &control, allocator) != 0) {
            ac_parser_set_error(error, error_size,
                                "out of memory while storing controls");
            return -1;
        }
    }

    return 0;
}

static int ac_parse_widgets(ac_topology_t* topology,
                            const uint8_t* payload,
                            size_t payload_size,
                            uint32_t count,
                            uint32_t block_index,
                            const ac_allocator_t* allocator,
                            char* error,
                            size_t error_size)
{
    size_t offset;
    uint32_t i;
    uint32_t control_index;
    const ac_tplg_dapm_widget_t* raw;
    ac_widget_t widget;
    ac_pipeline_t* pipeline;
    ac_control_t control;
    size_t private_offset;

    offset = 0u;
    for (i = 0u; i < count; i++) {
        if (payload_size - offset < sizeof(*raw)) {
            ac_parser_set_error(error, error_size,
                                "truncated widget block");
            return -1;
        }

        raw = (const ac_tplg_dapm_widget_t*)(const void*)(payload + offset);
        if (raw->size < sizeof(*raw) || raw->size > payload_size - offset) {
            ac_parser_set_error(error, error_size, "invalid widget size");
            return -1;
        }
        if (raw->priv_size > payload_size - offset - raw->size) {
            ac_parser_set_error(error, error_size,
                                "invalid widget private data size");
            return -1;
        }

        memset(&widget, 0, sizeof(widget));
        widget.id = raw->id;
        widget.pipeline_id = block_index;
        widget.block_index = block_index;
        widget.num_kcontrols = raw->num_kcontrols;
        ac_parser_copy(widget.name, sizeof(widget.name), raw->name,
                       sizeof(raw->name));
        ac_parser_copy(widget.stream_name, sizeof(widget.stream_name),
                       raw->sname, sizeof(raw->sname));

        if (raw->priv_size > 0u) {
            private_offset = raw->size;
            topology->summary.private_blocks++;
            if (ac_parse_vendor_arrays(payload + offset + private_offset,
                                       raw->priv_size, &widget.tokens,
                                       allocator, error, error_size) != 0)
                return -1;
        }

        pipeline = ac_append_pipeline(topology, block_index, allocator);
        if (!pipeline) {
            ac_parser_set_error(error, error_size,
                                "out of memory while storing pipeline");
            return -1;
        }
        pipeline->widget_count++;
        if (widget.id == AC_DAPM_SCHEDULER) {
            pipeline->has_scheduler = 1;
            ac_parser_copy(pipeline->name, sizeof(pipeline->name),
                           widget.name, strlen(widget.name));
        }

        offset += (size_t)raw->size + raw->priv_size;
        for (control_index = 0u; control_index < raw->num_kcontrols;
             control_index++) {
            ac_control_t summary_control;

            if (ac_parse_control(payload, payload_size, &offset, &control,
                                 allocator, 1) != 0) {
                ac_widget_release(&widget, allocator);
                ac_parser_set_error(error, error_size,
                                    "invalid widget control");
                return -1;
            }

            if (ac_append_widget_control(&widget.controls, &control,
                                         allocator) != 0) {
                ac_control_release(&control, allocator);
                ac_widget_release(&widget, allocator);
                ac_parser_set_error(error, error_size,
                                    "out of memory while storing control");
                return -1;
            }

            summary_control = control;
            summary_control.payload = 0;
            summary_control.payload_size = 0u;
            summary_control.payload_type = 0u;
            if (ac_append_control(topology, &summary_control, allocator) != 0) {
                ac_widget_release(&widget, allocator);
                ac_parser_set_error(error, error_size,
                                    "out of memory while storing control");
                return -1;
            }
        }

        if (ac_append_widget(topology, &widget, allocator) != 0) {
            ac_widget_release(&widget, allocator);
            ac_parser_set_error(error, error_size,
                                "out of memory while storing widget");
            return -1;
        }
    }

    return 0;
}

static int ac_parse_routes(ac_topology_t* topology,
                           const uint8_t* payload,
                           size_t payload_size,
                           uint32_t count,
                           const ac_allocator_t* allocator,
                           char* error,
                           size_t error_size)
{
    size_t offset;
    uint32_t i;
    const ac_tplg_dapm_graph_elem_t* raw;
    ac_route_t route;

    offset = 0u;
    for (i = 0u; i < count; i++) {
        if (payload_size - offset < sizeof(*raw)) {
            ac_parser_set_error(error, error_size,
                                "truncated route block");
            return -1;
        }

        raw = (const ac_tplg_dapm_graph_elem_t*)(const void*)(payload + offset);
        memset(&route, 0, sizeof(route));
        ac_parser_copy(route.sink, sizeof(route.sink), raw->sink,
                       sizeof(raw->sink));
        ac_parser_copy(route.control, sizeof(route.control), raw->control,
                       sizeof(raw->control));
        ac_parser_copy(route.source, sizeof(route.source), raw->source,
                       sizeof(raw->source));

        if (ac_append_route(topology, &route, allocator) != 0) {
            ac_parser_set_error(error, error_size,
                                "out of memory while storing route");
            return -1;
        }

        offset += sizeof(*raw);
    }

    return 0;
}

static void ac_copy_pcm_caps(ac_pcm_t* pcm,
                             const ac_tplg_stream_caps_t* caps,
                             uint32_t playback)
{
    if (playback) {
        ac_parser_copy(pcm->playback_stream, sizeof(pcm->playback_stream),
                       caps->name, sizeof(caps->name));
        pcm->playback_rate_max = caps->rate_max;
        pcm->playback_channels_max = caps->channels_max;
        pcm->playback_sig_bits = caps->sig_bits;
    } else {
        ac_parser_copy(pcm->capture_stream, sizeof(pcm->capture_stream),
                       caps->name, sizeof(caps->name));
        pcm->capture_rate_max = caps->rate_max;
        pcm->capture_channels_max = caps->channels_max;
        pcm->capture_sig_bits = caps->sig_bits;
    }
}

static int ac_parse_pcms(ac_topology_t* topology,
                         const uint8_t* payload,
                         size_t payload_size,
                         uint32_t count,
                         const ac_allocator_t* allocator,
                         char* error,
                         size_t error_size)
{
    size_t offset;
    uint32_t i;
    const ac_tplg_pcm_t* raw;
    ac_pcm_t pcm;

    offset = 0u;
    for (i = 0u; i < count; i++) {
        if (payload_size - offset < sizeof(*raw)) {
            ac_parser_set_error(error, error_size, "truncated PCM block");
            return -1;
        }

        raw = (const ac_tplg_pcm_t*)(const void*)(payload + offset);
        if (raw->size < sizeof(*raw) || raw->size > payload_size - offset ||
            raw->priv_size > payload_size - offset - raw->size) {
            ac_parser_set_error(error, error_size, "invalid PCM size");
            return -1;
        }

        memset(&pcm, 0, sizeof(pcm));
        ac_parser_copy(pcm.name, sizeof(pcm.name), raw->pcm_name,
                       sizeof(raw->pcm_name));
        ac_parser_copy(pcm.dai_name, sizeof(pcm.dai_name), raw->dai_name,
                       sizeof(raw->dai_name));
        pcm.id = raw->pcm_id;
        pcm.dai_id = raw->dai_id;
        pcm.playback = raw->playback;
        pcm.capture = raw->capture;
        ac_copy_pcm_caps(&pcm, &raw->caps[0], 1u);
        ac_copy_pcm_caps(&pcm, &raw->caps[1], 0u);

        if (raw->priv_size > 0u)
            topology->summary.private_blocks++;

        if (ac_append_pcm(topology, &pcm, allocator) != 0) {
            ac_parser_set_error(error, error_size,
                                "out of memory while storing PCM");
            return -1;
        }

        offset += (size_t)raw->size + raw->priv_size;
    }

    return 0;
}

static int ac_parse_dais(ac_topology_t* topology,
                         const uint8_t* payload,
                         size_t payload_size,
                         uint32_t count,
                         const ac_allocator_t* allocator,
                         char* error,
                         size_t error_size)
{
    size_t offset;
    uint32_t i;
    const ac_tplg_dai_t* raw;
    ac_dai_t dai;

    offset = 0u;
    for (i = 0u; i < count; i++) {
        if (payload_size - offset < sizeof(*raw)) {
            ac_parser_set_error(error, error_size, "truncated DAI block");
            return -1;
        }

        raw = (const ac_tplg_dai_t*)(const void*)(payload + offset);
        if (raw->size < sizeof(*raw) || raw->size > payload_size - offset ||
            raw->priv_size > payload_size - offset - raw->size) {
            ac_parser_set_error(error, error_size, "invalid DAI size");
            return -1;
        }

        memset(&dai, 0, sizeof(dai));
        ac_parser_copy(dai.name, sizeof(dai.name), raw->dai_name,
                       sizeof(raw->dai_name));
        dai.id = raw->dai_id;
        dai.playback = raw->playback;
        dai.capture = raw->capture;

        if (raw->priv_size > 0u)
            topology->summary.private_blocks++;

        if (ac_append_dai(topology, &dai, allocator) != 0) {
            ac_parser_set_error(error, error_size,
                                "out of memory while storing DAI");
            return -1;
        }

        offset += (size_t)raw->size + raw->priv_size;
    }

    return 0;
}

static int ac_parse_links(ac_topology_t* topology,
                          const uint8_t* payload,
                          size_t payload_size,
                          uint32_t count,
                          const ac_allocator_t* allocator,
                          char* error,
                          size_t error_size)
{
    size_t offset;
    uint32_t i;
    const ac_tplg_link_config_t* raw;
    ac_link_t link;

    offset = 0u;
    for (i = 0u; i < count; i++) {
        if (payload_size - offset < sizeof(*raw)) {
            ac_parser_set_error(error, error_size, "truncated link block");
            return -1;
        }

        raw = (const ac_tplg_link_config_t*)(const void*)(payload + offset);
        if (raw->size < sizeof(*raw) || raw->size > payload_size - offset ||
            raw->priv_size > payload_size - offset - raw->size) {
            ac_parser_set_error(error, error_size, "invalid link size");
            return -1;
        }

        memset(&link, 0, sizeof(link));
        ac_parser_copy(link.name, sizeof(link.name), raw->name,
                       sizeof(raw->name));
        ac_parser_copy(link.stream_name, sizeof(link.stream_name),
                       raw->stream_name, sizeof(raw->stream_name));
        link.id = raw->id;
        link.num_hw_configs = raw->num_hw_configs;
        link.default_hw_config_id = raw->default_hw_config_id;

        if (raw->priv_size > 0u)
            topology->summary.private_blocks++;

        if (ac_append_link(topology, &link, allocator) != 0) {
            ac_parser_set_error(error, error_size,
                                "out of memory while storing link");
            return -1;
        }

        offset += (size_t)raw->size + raw->priv_size;
    }

    return 0;
}

static int ac_parse_manifest(ac_topology_t* topology,
                             const uint8_t* payload,
                             size_t payload_size,
                             char* error,
                             size_t error_size)
{
    const ac_tplg_manifest_t* raw;

    if (payload_size < sizeof(*raw)) {
        ac_parser_set_error(error, error_size, "truncated manifest block");
        return -1;
    }

    raw = (const ac_tplg_manifest_t*)(const void*)payload;
    if (raw->size < sizeof(*raw) || raw->size > payload_size ||
        raw->priv_size > payload_size - raw->size) {
        ac_parser_set_error(error, error_size, "invalid manifest size");
        return -1;
    }

    topology->summary.manifests++;
    if (raw->priv_size > 0u)
        topology->summary.private_blocks++;

    return 0;
}

const ac_widget_t* ac_find_widget_by_name(const ac_topology_t* topology,
                                          const char* name)
{
    size_t i;

    if (!topology || !name)
        return 0;

    for (i = 0u; i < topology->widget_count; i++) {
        if (strcmp(topology->widgets[i].name, name) == 0)
            return &topology->widgets[i];
    }

    return 0;
}

static void ac_update_pipeline_from_pcm(ac_pipeline_t* pipeline,
                                        const ac_pcm_t* pcm,
                                        uint32_t playback)
{
    uint32_t rate;
    uint32_t channels;
    uint32_t sig_bits;

    if (playback) {
        rate = pcm->playback_rate_max;
        channels = pcm->playback_channels_max;
        sig_bits = pcm->playback_sig_bits;
    } else {
        rate = pcm->capture_rate_max;
        channels = pcm->capture_channels_max;
        sig_bits = pcm->capture_sig_bits;
    }

    if (rate != 0u)
        pipeline->max_sample_rate = rate;
    if (channels != 0u)
        pipeline->max_channels = channels;
    if (sig_bits != 0u)
        pipeline->max_bytes_per_sample = (sig_bits + 7u) / 8u;
}

static void ac_update_pipeline_caps(ac_topology_t* topology)
{
    size_t i;
    size_t j;
    ac_pipeline_t* pipeline;
    const ac_widget_t* widget;
    const ac_pcm_t* pcm;

    for (i = 0u; i < topology->widget_count; i++) {
        widget = &topology->widgets[i];
        if (widget->stream_name[0] == '\0')
            continue;

        pipeline = ac_find_pipeline_by_id(topology, widget->pipeline_id);
        if (!pipeline)
            continue;

        for (j = 0u; j < topology->pcm_count; j++) {
            pcm = &topology->pcms[j];
            if (strcmp(widget->stream_name, pcm->playback_stream) == 0)
                ac_update_pipeline_from_pcm(pipeline, pcm, 1u);
            if (strcmp(widget->stream_name, pcm->capture_stream) == 0)
                ac_update_pipeline_from_pcm(pipeline, pcm, 0u);
        }
    }
}

static void ac_count_pipeline_routes(ac_topology_t* topology)
{
    size_t i;
    const ac_widget_t* source;
    const ac_widget_t* sink;
    ac_pipeline_t* pipeline;

    for (i = 0u; i < topology->route_count; i++) {
        source = ac_find_widget_by_name(topology, topology->routes[i].source);
        sink = ac_find_widget_by_name(topology, topology->routes[i].sink);
        if (!source || !sink)
            continue;

        pipeline = ac_find_pipeline_by_id(topology, sink->pipeline_id);
        if (pipeline)
            pipeline->route_count++;
    }
}

void ac_topology_init(ac_topology_t* topology)
{
    if (topology)
        memset(topology, 0, sizeof(*topology));
}

void ac_topology_clear(ac_topology_t* topology,
                       const ac_allocator_t* allocator)
{
    size_t i;

    if (!topology || !allocator)
        return;

    for (i = 0u; i < topology->widget_count; i++)
        ac_widget_release(&topology->widgets[i], allocator);

    for (i = 0u; i < topology->control_count; i++)
        ac_control_release(&topology->controls[i], allocator);

    ac_parser_free(allocator, topology->pcms);
    ac_parser_free(allocator, topology->dais);
    ac_parser_free(allocator, topology->links);
    ac_parser_free(allocator, topology->widgets);
    ac_parser_free(allocator, topology->routes);
    ac_parser_free(allocator, topology->controls);
    ac_parser_free(allocator, topology->pipelines);
    ac_topology_init(topology);
}

int ac_parse_topology(const void* data,
                      size_t size,
                      const ac_allocator_t* allocator,
                      ac_topology_t* topology,
                      char* error,
                      size_t error_size)
{
    const uint8_t* bytes;
    size_t offset;
    const ac_tplg_hdr_t* hdr;
    const uint8_t* payload;
    int ret;

    if (!data || !allocator || !topology) {
        ac_parser_set_error(error, error_size, "invalid parse argument");
        return -1;
    }

    bytes = (const uint8_t*)data;
    offset = 0u;
    ac_topology_clear(topology, allocator);

    while (offset < size) {
        if (size - offset < sizeof(*hdr)) {
            ac_parser_set_error(error, error_size,
                                "truncated topology header");
            return -1;
        }

        hdr = (const ac_tplg_hdr_t*)(const void*)(bytes + offset);
        if (hdr->magic != AC_TPLG_MAGIC) {
            ac_parser_set_error_uint(error, error_size,
                                     "invalid topology magic at offset ",
                                     (uint32_t)offset);
            return -1;
        }
        if (hdr->abi < AC_TPLG_ABI_VERSION_MIN ||
            hdr->abi > AC_TPLG_ABI_VERSION) {
            ac_parser_set_error_uint(error, error_size,
                                     "unsupported topology ABI ", hdr->abi);
            return -1;
        }
        if (hdr->size < sizeof(*hdr) || hdr->size > size - offset) {
            ac_parser_set_error(error, error_size,
                                "invalid topology header size");
            return -1;
        }
        if (hdr->payload_size > size - offset - hdr->size) {
            ac_parser_set_error(error, error_size,
                                "invalid topology payload size");
            return -1;
        }

        topology->summary.abi = hdr->abi;
        payload = bytes + offset + hdr->size;
        ret = 0;

        switch (hdr->type) {
        case AC_TPLG_TYPE_MANIFEST:
            ret = ac_parse_manifest(topology, payload, hdr->payload_size,
                                    error, error_size);
            break;
        case AC_TPLG_TYPE_MIXER:
        case AC_TPLG_TYPE_BYTES:
        case AC_TPLG_TYPE_ENUM:
            ret = ac_parse_controls(topology, payload, hdr->payload_size,
                                    hdr->count, hdr->type, allocator, error,
                                    error_size);
            break;
        case AC_TPLG_TYPE_DAPM_WIDGET:
            ret = ac_parse_widgets(topology, payload, hdr->payload_size,
                                   hdr->count, hdr->index, allocator, error,
                                   error_size);
            break;
        case AC_TPLG_TYPE_DAPM_GRAPH:
            ret = ac_parse_routes(topology, payload, hdr->payload_size,
                                  hdr->count, allocator, error, error_size);
            break;
        case AC_TPLG_TYPE_PCM:
            ret = ac_parse_pcms(topology, payload, hdr->payload_size,
                                hdr->count, allocator, error, error_size);
            break;
        case AC_TPLG_TYPE_DAI:
            ret = ac_parse_dais(topology, payload, hdr->payload_size,
                                hdr->count, allocator, error, error_size);
            break;
        case AC_TPLG_TYPE_DAI_LINK:
        case AC_TPLG_TYPE_BACKEND_LINK:
            ret = ac_parse_links(topology, payload, hdr->payload_size,
                                 hdr->count, allocator, error, error_size);
            break;
        case AC_TPLG_TYPE_PDATA:
            topology->summary.private_blocks++;
            break;
        default:
            break;
        }

        if (ret != 0)
            return -1;

        offset += hdr->size + hdr->payload_size;
    }

    ac_update_pipeline_caps(topology);
    ac_count_pipeline_routes(topology);
    return 0;
}

static int ac_format_append(char** cursor,
                            size_t* remaining,
                            const char* text)
{
    size_t len;

    if (!cursor || !*cursor || !remaining || *remaining == 0u)
        return -1;

    len = strlen(text);
    if (len >= *remaining) {
        *remaining = 0u;
        return -1;
    }

    memcpy(*cursor, text, len);
    *cursor += len;
    **cursor = '\0';
    *remaining -= len;
    return 0;
}

static int ac_format_append_uint(char** cursor,
                                 size_t* remaining,
                                 uint32_t value)
{
    char text[16];

    text[0] = '\0';
    ac_parser_append_uint(text, sizeof(text), value);
    return ac_format_append(cursor, remaining, text);
}

int ac_topology_format_list(const ac_topology_t* topology,
                            char* buffer,
                            size_t buffer_size)
{
    char* cursor;
    size_t remaining;
    size_t i;
    int ret;

    if (!topology || !buffer || buffer_size == 0u)
        return -1;

    cursor = buffer;
    remaining = buffer_size;
    buffer[0] = '\0';

    ret = ac_format_append(&cursor, &remaining, "topology: abi ");
    ret |= ac_format_append_uint(&cursor, &remaining, topology->summary.abi);
    ret |= ac_format_append(&cursor, &remaining, "\npipelines: ");
    ret |= ac_format_append_uint(&cursor, &remaining,
                                 topology->summary.pipelines);
    ret |= ac_format_append(&cursor, &remaining, "\n");
    if (ret != 0)
        return -1;

    for (i = 0u; i < topology->pipeline_count; i++) {
        ret = ac_format_append(&cursor, &remaining, "  - ");
        ret |= ac_format_append(&cursor, &remaining,
                                topology->pipelines[i].name);
        ret |= ac_format_append(&cursor, &remaining, " id:");
        ret |= ac_format_append_uint(&cursor, &remaining,
                                     topology->pipelines[i].id);
        ret |= ac_format_append(&cursor, &remaining, " widgets:");
        ret |= ac_format_append_uint(&cursor, &remaining,
                                     topology->pipelines[i].widget_count);
        ret |= ac_format_append(&cursor, &remaining, " routes:");
        ret |= ac_format_append_uint(&cursor, &remaining,
                                     topology->pipelines[i].route_count);
        ret |= ac_format_append(&cursor, &remaining, " scheduler:");
        ret |= ac_format_append(&cursor, &remaining,
                                topology->pipelines[i].has_scheduler ?
                                "yes\n" : "no\n");
        if (ret != 0)
            return -1;
    }

    ret = ac_format_append(&cursor, &remaining, "pcms: ");
    ret |= ac_format_append_uint(&cursor, &remaining, topology->summary.pcms);
    ret |= ac_format_append(&cursor, &remaining, "\ndais: ");
    ret |= ac_format_append_uint(&cursor, &remaining, topology->summary.dais);
    ret |= ac_format_append(&cursor, &remaining, "\nlinks: ");
    ret |= ac_format_append_uint(&cursor, &remaining, topology->summary.links);
    ret |= ac_format_append(&cursor, &remaining, "\nwidgets: ");
    ret |= ac_format_append_uint(&cursor, &remaining,
                                 topology->summary.widgets);
    ret |= ac_format_append(&cursor, &remaining, "\nroutes: ");
    ret |= ac_format_append_uint(&cursor, &remaining,
                                 topology->summary.routes);
    ret |= ac_format_append(&cursor, &remaining, "\ncontrols: ");
    ret |= ac_format_append_uint(&cursor, &remaining,
                                 topology->summary.controls);
    ret |= ac_format_append(&cursor, &remaining, "\nprivate_blocks: ");
    ret |= ac_format_append_uint(&cursor, &remaining,
                                 topology->summary.private_blocks);
    ret |= ac_format_append(&cursor, &remaining, "\n");

    return ret == 0 ? 0 : -1;
}

const ac_token_t* ac_find_token(const ac_token_list_t* tokens, uint32_t token)
{
    size_t i;

    if (!tokens)
        return 0;

    for (i = 0u; i < tokens->count; i++) {
        if (tokens->items[i].token == token)
            return &tokens->items[i];
    }

    return 0;
}
