#include "audio_controller_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void* default_alloc(void* user, size_t size, size_t alignment) {
  (void)user;
  (void)alignment;
  return calloc(1u, size == 0u ? 1u : size);
}

static void default_free(void* user, void* ptr) {
  (void)user;
  free(ptr);
}

static void default_log(void* user, audio_controller_log_level_t level, const char* message) {
  (void)user;
  (void)level;
  (void)message;
}

void ac_set_error(audio_controller_t* controller, const char* fmt, ...) {
  va_list args;
  if (controller == NULL) return;
  va_start(args, fmt);
  (void)vsnprintf(controller->last_error, sizeof(controller->last_error), fmt, args);
  va_end(args);
  controller->last_error[sizeof(controller->last_error) - 1u] = '\0';
  if (controller->platform.log != NULL) {
    controller->platform.log(controller->platform.user, AUDIO_CONTROLLER_LOG_ERROR, controller->last_error);
  }
}

audio_controller_t* audio_controller_create(const audio_controller_create_params_t* params) {
  audio_controller_platform_ops_t platform;
  audio_controller_t* controller;
  memset(&platform, 0, sizeof(platform));
  if (params != NULL && params->platform != NULL) platform = *params->platform;
  if (platform.alloc == NULL) platform.alloc = default_alloc;
  if (platform.free == NULL) platform.free = default_free;
  if (platform.log == NULL) platform.log = default_log;

  controller = (audio_controller_t*)platform.alloc(platform.user, sizeof(*controller), sizeof(void*));
  if (controller == NULL) return NULL;
  memset(controller, 0, sizeof(*controller));
  controller->platform = platform;
  controller->allocator.user = platform.user;
  controller->allocator.alloc = platform.alloc;
  controller->allocator.free = platform.free;
  controller->verbose = params != NULL ? params->verbose : 0;
  ac_topology_init(&controller->topology);
  return controller;
}

void audio_controller_destroy(audio_controller_t* controller) {
  audio_controller_platform_ops_t platform;
  if (controller == NULL) return;
  platform = controller->platform;
  ac_topology_clear(&controller->topology, &controller->allocator);
  platform.free(platform.user, controller);
}

int audio_controller_load_topology_buffer(audio_controller_t* controller, const void* data, size_t size) {
  char error[256];
  if (controller == NULL || data == NULL || size == 0u) return -1;
  error[0] = '\0';
  if (ac_parse_topology(data, size, &controller->allocator, &controller->topology, error, sizeof(error)) != 0) {
    ac_set_error(controller, "%s", error[0] != '\0' ? error : "failed to parse topology");
    return -1;
  }
  controller->last_error[0] = '\0';
  return 0;
}

int audio_controller_load_topology_file(audio_controller_t* controller, const char* path) {
  FILE* file;
  long file_size;
  void* data;
  size_t read_size;
  int result;
  if (controller == NULL || path == NULL || path[0] == '\0') return -1;
  file = fopen(path, "rb");
  if (file == NULL) {
    ac_set_error(controller, "failed to open topology file: %s", path);
    return -1;
  }
  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    ac_set_error(controller, "failed to seek topology file: %s", path);
    return -1;
  }
  file_size = ftell(file);
  if (file_size <= 0) {
    fclose(file);
    ac_set_error(controller, "empty topology file: %s", path);
    return -1;
  }
  if (fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    ac_set_error(controller, "failed to rewind topology file: %s", path);
    return -1;
  }
  data = controller->platform.alloc(controller->platform.user, (size_t)file_size, sizeof(void*));
  if (data == NULL) {
    fclose(file);
    ac_set_error(controller, "out of memory while loading topology");
    return -1;
  }
  read_size = fread(data, 1u, (size_t)file_size, file);
  fclose(file);
  if (read_size != (size_t)file_size) {
    controller->platform.free(controller->platform.user, data);
    ac_set_error(controller, "failed to read topology file: %s", path);
    return -1;
  }
  result = audio_controller_load_topology_buffer(controller, data, (size_t)file_size);
  controller->platform.free(controller->platform.user, data);
  return result;
}

int audio_controller_list_pipelines(audio_controller_t* controller, char* buffer, size_t buffer_size) {
  if (controller == NULL || buffer == NULL || buffer_size == 0u) return -1;
  if (ac_topology_format_list(&controller->topology, buffer, buffer_size) != 0) {
    ac_set_error(controller, "pipeline list output buffer is too small");
    return -1;
  }
  return 0;
}

int audio_controller_get_summary(audio_controller_t* controller, audio_controller_topology_summary_t* summary) {
  if (controller == NULL || summary == NULL) return -1;
  *summary = controller->topology.summary;
  return 0;
}

int audio_controller_start_pipeline(audio_controller_t* controller, const char* id_or_name) {
  (void)id_or_name;
  ac_set_error(controller, "pipeline start is not implemented in audio_controller v1");
  return -1;
}

int audio_controller_stop_pipeline(audio_controller_t* controller, const char* id_or_name) {
  (void)id_or_name;
  ac_set_error(controller, "pipeline stop is not implemented in audio_controller v1");
  return -1;
}

int audio_controller_apply_preset(audio_controller_t* controller, const char* preset_id) {
  (void)preset_id;
  ac_set_error(controller, "preset apply is not implemented in audio_controller v1");
  return -1;
}

const char* audio_controller_get_last_error(audio_controller_t* controller) {
  if (controller == NULL) return "audio_controller is null";
  return controller->last_error[0] != '\0' ? controller->last_error : "";
}
