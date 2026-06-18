#ifndef AUDIO_CONTROLLER_TOPOLOGY_UAPI_H_
#define AUDIO_CONTROLLER_TOPOLOGY_UAPI_H_

#include <stdint.h>

#define AC_TPLG_MAGIC 0x41536f43u
#define AC_TPLG_ABI_VERSION_MIN 0x4u
#define AC_TPLG_ABI_VERSION 0x5u
#define AC_TPLG_NAME_SIZE 44u
#define AC_TPLG_NUM_TEXTS 16u
#define AC_TPLG_MAX_CHAN 8u
#define AC_TPLG_STREAM_CONFIG_MAX 8u
#define AC_TPLG_HW_CONFIG_MAX 8u

#define AC_TPLG_TYPE_MIXER 1u
#define AC_TPLG_TYPE_BYTES 2u
#define AC_TPLG_TYPE_ENUM 3u
#define AC_TPLG_TYPE_DAPM_GRAPH 4u
#define AC_TPLG_TYPE_DAPM_WIDGET 5u
#define AC_TPLG_TYPE_DAI_LINK 6u
#define AC_TPLG_TYPE_PCM 7u
#define AC_TPLG_TYPE_MANIFEST 8u
#define AC_TPLG_TYPE_BACKEND_LINK 10u
#define AC_TPLG_TYPE_PDATA 11u
#define AC_TPLG_TYPE_DAI 12u

#define AC_TPLG_TUPLE_TYPE_UUID 0u
#define AC_TPLG_TUPLE_TYPE_STRING 1u
#define AC_TPLG_TUPLE_TYPE_BOOL 2u
#define AC_TPLG_TUPLE_TYPE_BYTE 3u
#define AC_TPLG_TUPLE_TYPE_WORD 4u
#define AC_TPLG_TUPLE_TYPE_SHORT 5u

#define AC_DAPM_MUX 2u
#define AC_DAPM_MIXER 3u
#define AC_DAPM_PGA 4u
#define AC_DAPM_AIF_IN 11u
#define AC_DAPM_AIF_OUT 12u
#define AC_DAPM_DAI_IN 13u
#define AC_DAPM_DAI_OUT 14u
#define AC_DAPM_SCHEDULER 17u
#define AC_DAPM_EFFECT 18u
#define AC_DAPM_SRC 20u
#define AC_DAPM_ASRC 21u

#define AC_SOF_TKN_DAI_TYPE 154u
#define AC_SOF_TKN_DAI_INDEX 155u
#define AC_SOF_TKN_DAI_DIRECTION 156u
#define AC_SOF_TKN_SCHED_PERIOD 200u
#define AC_SOF_TKN_SCHED_PRIORITY 201u
#define AC_SOF_TKN_SCHED_MIPS 202u
#define AC_SOF_TKN_SCHED_CORE 203u
#define AC_SOF_TKN_SCHED_FRAMES 204u
#define AC_SOF_TKN_SCHED_TIME_DOMAIN 205u
#define AC_SOF_TKN_SCHED_DYNAMIC_PIPELINE 206u
#define AC_SOF_TKN_SRC_RATE_IN 300u
#define AC_SOF_TKN_SRC_RATE_OUT 301u
#define AC_SOF_TKN_ASRC_RATE_IN 320u
#define AC_SOF_TKN_ASRC_RATE_OUT 321u
#define AC_SOF_TKN_ASRC_ASYNCHRONOUS_MODE 322u
#define AC_SOF_TKN_ASRC_OPERATION_MODE 323u
#define AC_SOF_TKN_COMP_PERIOD_SINK_COUNT 400u
#define AC_SOF_TKN_COMP_PERIOD_SOURCE_COUNT 401u
#define AC_SOF_TKN_COMP_FORMAT 402u
#define AC_SOF_TKN_COMP_CORE_ID 404u
#define AC_SOF_TKN_COMP_UUID 405u
#define AC_SOF_TKN_PROCESS_TYPE 900u

#pragma pack(push, 1)
typedef struct ac_tplg_hdr {
  uint32_t magic;
  uint32_t abi;
  uint32_t version;
  uint32_t type;
  uint32_t size;
  uint32_t vendor_type;
  uint32_t payload_size;
  uint32_t index;
  uint32_t count;
} ac_tplg_hdr_t;

typedef struct ac_tplg_vendor_array {
  uint32_t size;
  uint32_t type;
  uint32_t num_elems;
} ac_tplg_vendor_array_t;

typedef struct ac_tplg_vendor_value_elem {
  uint32_t token;
  uint32_t value;
} ac_tplg_vendor_value_elem_t;

typedef struct ac_tplg_vendor_uuid_elem {
  uint32_t token;
  uint8_t uuid[16];
} ac_tplg_vendor_uuid_elem_t;

typedef struct ac_tplg_vendor_string_elem {
  uint32_t token;
  char string[AC_TPLG_NAME_SIZE];
} ac_tplg_vendor_string_elem_t;

typedef struct ac_tplg_stream {
  uint32_t size;
  char name[AC_TPLG_NAME_SIZE];
  uint64_t format;
  uint32_t rate;
  uint32_t period_bytes;
  uint32_t buffer_bytes;
  uint32_t channels;
} ac_tplg_stream_t;

typedef struct ac_tplg_stream_caps {
  uint32_t size;
  char name[AC_TPLG_NAME_SIZE];
  uint64_t formats;
  uint32_t rates;
  uint32_t rate_min;
  uint32_t rate_max;
  uint32_t channels_min;
  uint32_t channels_max;
  uint32_t periods_min;
  uint32_t periods_max;
  uint32_t period_size_min;
  uint32_t period_size_max;
  uint32_t buffer_size_min;
  uint32_t buffer_size_max;
  uint32_t sig_bits;
} ac_tplg_stream_caps_t;

typedef struct ac_tplg_hw_config {
  uint32_t size;
  uint32_t id;
  uint32_t fmt;
  uint8_t clock_gated;
  uint8_t invert_bclk;
  uint8_t invert_fsync;
  uint8_t bclk_provider;
  uint8_t fsync_provider;
  uint8_t mclk_direction;
  uint16_t reserved;
  uint32_t mclk_rate;
  uint32_t bclk_rate;
  uint32_t fsync_rate;
  uint32_t tdm_slots;
  uint32_t tdm_slot_width;
  uint32_t tx_slots;
  uint32_t rx_slots;
  uint32_t tx_channels;
  uint32_t tx_chanmap[AC_TPLG_MAX_CHAN];
  uint32_t rx_channels;
  uint32_t rx_chanmap[AC_TPLG_MAX_CHAN];
} ac_tplg_hw_config_t;

typedef struct ac_tplg_dapm_graph_elem {
  char sink[AC_TPLG_NAME_SIZE];
  char control[AC_TPLG_NAME_SIZE];
  char source[AC_TPLG_NAME_SIZE];
} ac_tplg_dapm_graph_elem_t;

typedef struct ac_tplg_dapm_widget {
  uint32_t size;
  uint32_t id;
  char name[AC_TPLG_NAME_SIZE];
  char sname[AC_TPLG_NAME_SIZE];
  uint32_t reg;
  uint32_t shift;
  uint32_t mask;
  uint32_t subseq;
  uint32_t invert;
  uint32_t ignore_suspend;
  uint16_t event_flags;
  uint16_t event_type;
  uint32_t num_kcontrols;
  uint32_t priv_size;
} ac_tplg_dapm_widget_t;

typedef struct ac_tplg_pcm {
  uint32_t size;
  char pcm_name[AC_TPLG_NAME_SIZE];
  char dai_name[AC_TPLG_NAME_SIZE];
  uint32_t pcm_id;
  uint32_t dai_id;
  uint32_t playback;
  uint32_t capture;
  uint32_t compress;
  ac_tplg_stream_t stream[AC_TPLG_STREAM_CONFIG_MAX];
  uint32_t num_streams;
  ac_tplg_stream_caps_t caps[2];
  uint32_t flag_mask;
  uint32_t flags;
  uint32_t priv_size;
} ac_tplg_pcm_t;

typedef struct ac_tplg_link_config {
  uint32_t size;
  uint32_t id;
  char name[AC_TPLG_NAME_SIZE];
  char stream_name[AC_TPLG_NAME_SIZE];
  ac_tplg_stream_t stream[AC_TPLG_STREAM_CONFIG_MAX];
  uint32_t num_streams;
  ac_tplg_hw_config_t hw_config[AC_TPLG_HW_CONFIG_MAX];
  uint32_t num_hw_configs;
  uint32_t default_hw_config_id;
  uint32_t flag_mask;
  uint32_t flags;
  uint32_t priv_size;
} ac_tplg_link_config_t;

typedef struct ac_tplg_dai {
  uint32_t size;
  char dai_name[AC_TPLG_NAME_SIZE];
  uint32_t dai_id;
  uint32_t playback;
  uint32_t capture;
  ac_tplg_stream_caps_t caps[2];
  uint32_t flag_mask;
  uint32_t flags;
  uint32_t priv_size;
} ac_tplg_dai_t;

typedef struct ac_tplg_manifest {
  uint32_t size;
  uint32_t control_elems;
  uint32_t widget_elems;
  uint32_t graph_elems;
  uint32_t pcm_elems;
  uint32_t dai_link_elems;
  uint32_t dai_elems;
  uint32_t reserved[20];
  uint32_t priv_size;
} ac_tplg_manifest_t;

typedef struct ac_tplg_ctl_hdr {
  uint32_t size;
  uint32_t type;
  char name[AC_TPLG_NAME_SIZE];
} ac_tplg_ctl_hdr_t;
#pragma pack(pop)

#endif
