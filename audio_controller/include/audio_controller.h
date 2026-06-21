#ifndef AUDIO_CONTROLLER_H_
#define AUDIO_CONTROLLER_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct audio_controller audio_controller_t;
struct sof_pipe;

#define AUDIO_CONTROLLER_MAX_INSTALLED_PIPELINES 16u

typedef enum audio_controller_log_level {
    AUDIO_CONTROLLER_LOG_ERROR = 0,
    AUDIO_CONTROLLER_LOG_WARN = 1,
    AUDIO_CONTROLLER_LOG_INFO = 2,
    AUDIO_CONTROLLER_LOG_DEBUG = 3,
} audio_controller_log_level_t;

typedef void* audio_controller_thread_t;
typedef void* audio_controller_mutex_t;

typedef struct audio_controller_datalink_device_ops {
    void* user;
    int (*open)(void* user);
    void (*close)(void* user);
    int (*read)(void* user, void* buffer, size_t capacity,
                size_t* actual_size, unsigned int timeout_ms);
    int (*write)(void* user, const void* data, size_t size,
                 unsigned int timeout_ms);
    size_t (*mtu)(void* user);
} audio_controller_datalink_device_ops_t;

typedef struct audio_controller_log_source_ops {
    void* user;
    int (*open)(void* user);
    int (*start)(void* user);
    int (*read)(void* user, void* buffer, size_t capacity,
                size_t* actual_size, unsigned int timeout_ms);
    void (*stop)(void* user);
    void (*close)(void* user);
} audio_controller_log_source_ops_t;

typedef struct audio_controller_driver_ops {
    void* user;
    void* (*alloc)(void* user, size_t size, size_t alignment);
    void (*free)(void* user, void* ptr);
    void (*log)(void* user, audio_controller_log_level_t level,
                const char* message);
    int (*thread_create)(void* user,
                         audio_controller_thread_t* thread,
                         void* (*entry)(void*),
                         void* arg);
    int (*thread_join)(void* user, audio_controller_thread_t thread);
    int (*mutex_create)(void* user, audio_controller_mutex_t* mutex);
    void (*mutex_destroy)(void* user, audio_controller_mutex_t mutex);
    int (*mutex_lock)(void* user, audio_controller_mutex_t mutex);
    int (*mutex_unlock)(void* user, audio_controller_mutex_t mutex);
    const audio_controller_datalink_device_ops_t* datalink;
    const audio_controller_log_source_ops_t* log_source;
} audio_controller_driver_ops_t;

typedef struct audio_controller_create_params {
    const audio_controller_driver_ops_t* driver;
    int verbose;
} audio_controller_create_params_t;

typedef struct audio_controller_topology_summary {
    uint32_t abi;
    uint32_t manifests;
    uint32_t pcms;
    uint32_t dais;
    uint32_t links;
    uint32_t widgets;
    uint32_t routes;
    uint32_t controls;
    uint32_t pipelines;
    uint32_t private_blocks;
} audio_controller_topology_summary_t;

typedef struct audio_controller_installed_pipeline {
    uint32_t pipe_id;
    uint32_t max_sample_rate;
    uint32_t max_channels;
    struct sof_pipe* pipeline;
} audio_controller_installed_pipeline_t;

typedef struct audio_controller_installed_pipelines {
    uint32_t pipe_id;
    uint32_t count;
    audio_controller_installed_pipeline_t
        pipelines[AUDIO_CONTROLLER_MAX_INSTALLED_PIPELINES];
} audio_controller_installed_pipelines_t;

typedef struct audio_controller_transport_stats {
    int initialized;
    int running;
    int datalink_open;
    uint32_t datalink_mtu;
    uint32_t tx_packets;
    uint32_t rx_packets;
    uint32_t retries;
} audio_controller_transport_stats_t;

audio_controller_t* audio_controller_create(const audio_controller_create_params_t* params);
void audio_controller_destroy(audio_controller_t* controller);

int audio_controller_load_topology_buffer(audio_controller_t* controller, const void* data, size_t size);
int audio_controller_list_pipelines(audio_controller_t* controller, char* buffer, size_t buffer_size);
int audio_controller_install_pipeline(audio_controller_t* controller,
                                      const char* id_or_name,
                                      audio_controller_installed_pipelines_t* installed);
int audio_controller_install_all(audio_controller_t* controller,
                                 audio_controller_installed_pipelines_t* installed);
int audio_controller_get_summary(audio_controller_t* controller, audio_controller_topology_summary_t* summary);
int audio_controller_get_transport_stats(audio_controller_t* controller,
                                         audio_controller_transport_stats_t* stats);
const char* audio_controller_get_last_error(audio_controller_t* controller);

#ifdef __cplusplus
}
#endif

#endif
