#ifndef AUDIO_CONTROLLER_TOPOLOGY_PARSER_H_
#define AUDIO_CONTROLLER_TOPOLOGY_PARSER_H_

#include "audio_controller.h"

#include <stddef.h>
#include <stdint.h>

#define AC_MAX_NAME 64
#define AC_UUID_SIZE 16

typedef enum ac_token_type {
  AC_TOKEN_VALUE,
  AC_TOKEN_UUID,
  AC_TOKEN_STRING,
} ac_token_type_t;

typedef struct ac_token {
    uint32_t token;
    ac_token_type_t type;
    uint32_t value;
    uint8_t uuid[AC_UUID_SIZE];
    char string[AC_MAX_NAME];
} ac_token_t;

typedef struct ac_token_list {
    ac_token_t* items;
    size_t count;
    size_t capacity;
} ac_token_list_t;

typedef struct ac_control {
    char name[AC_MAX_NAME];
    uint32_t type;
    uint32_t payload_type;
    uint32_t payload_size;
    uint8_t* payload;
} ac_control_t;

typedef struct ac_control_list {
    ac_control_t* items;
    size_t count;
    size_t capacity;
} ac_control_list_t;

typedef struct ac_widget {
    char name[AC_MAX_NAME];
    char stream_name[AC_MAX_NAME];
    uint32_t id;
    uint32_t pipeline_id;
    uint32_t block_index;
    uint32_t num_kcontrols;
    ac_token_list_t tokens;
    ac_control_list_t controls;
} ac_widget_t;

typedef struct ac_route {
    char sink[AC_MAX_NAME];
    char control[AC_MAX_NAME];
    char source[AC_MAX_NAME];
} ac_route_t;

typedef struct ac_pcm {
    char name[AC_MAX_NAME];
    char dai_name[AC_MAX_NAME];
    char playback_stream[AC_MAX_NAME];
    char capture_stream[AC_MAX_NAME];
    uint32_t id;
    uint32_t dai_id;
    uint32_t playback;
    uint32_t capture;
    uint32_t playback_rate_max;
    uint32_t playback_channels_max;
    uint32_t playback_sig_bits;
    uint32_t capture_rate_max;
    uint32_t capture_channels_max;
    uint32_t capture_sig_bits;
} ac_pcm_t;

typedef struct ac_dai {
    char name[AC_MAX_NAME];
    uint32_t id;
    uint32_t playback;
    uint32_t capture;
} ac_dai_t;

typedef struct ac_link {
    char name[AC_MAX_NAME];
    char stream_name[AC_MAX_NAME];
    uint32_t id;
    uint32_t num_hw_configs;
    uint32_t default_hw_config_id;
} ac_link_t;

typedef struct ac_pipeline {
    char name[AC_MAX_NAME];
    uint32_t id;
    uint32_t index;
    uint32_t widget_count;
    uint32_t route_count;
    uint32_t max_sample_rate;
    uint32_t max_channels;
    uint32_t max_bytes_per_sample;
    int has_scheduler;
} ac_pipeline_t;

typedef struct ac_topology {
    audio_controller_topology_summary_t summary;
    ac_pcm_t* pcms;
    size_t pcm_count;
    size_t pcm_capacity;
    ac_dai_t* dais;
    size_t dai_count;
    size_t dai_capacity;
    ac_link_t* links;
    size_t link_count;
    size_t link_capacity;
    ac_widget_t* widgets;
    size_t widget_count;
    size_t widget_capacity;
    ac_route_t* routes;
    size_t route_count;
    size_t route_capacity;
    ac_control_t* controls;
    size_t control_count;
    size_t control_capacity;
    ac_pipeline_t* pipelines;
    size_t pipeline_count;
    size_t pipeline_capacity;
} ac_topology_t;

typedef struct ac_allocator {
    void* user;
    void* (*alloc)(void* user, size_t size, size_t alignment);
    void (*free)(void* user, void* ptr);
} ac_allocator_t;

void ac_topology_init(ac_topology_t* topology);
void ac_topology_clear(ac_topology_t* topology, const ac_allocator_t* allocator);
int ac_parse_topology(const void* data,
                      size_t size,
                      const ac_allocator_t* allocator,
                      ac_topology_t* topology,
                      char* error,
                      size_t error_size);
int ac_topology_format_list(const ac_topology_t* topology, char* buffer, size_t buffer_size);
const ac_token_t* ac_find_token(const ac_token_list_t* tokens, uint32_t token);
const ac_widget_t* ac_find_widget_by_name(const ac_topology_t* topology,
                                          const char* name);

#endif
