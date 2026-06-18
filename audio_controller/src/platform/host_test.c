#include "audio_controller/audio_controller.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void* host_alloc(void* user, size_t size, size_t alignment) {
  (void)user;
  (void)alignment;
  return calloc(1u, size == 0u ? 1u : size);
}

static void host_free(void* user, void* ptr) {
  (void)user;
  free(ptr);
}

static void host_log(void* user, audio_controller_log_level_t level, const char* message) {
  const char* prefix = "info";
  (void)user;
  if (level == AUDIO_CONTROLLER_LOG_ERROR) prefix = "error";
  else if (level == AUDIO_CONTROLLER_LOG_WARN) prefix = "warn";
  else if (level == AUDIO_CONTROLLER_LOG_DEBUG) prefix = "debug";
  fprintf(stderr, "audio_controller[%s]: %s\n", prefix, message != NULL ? message : "");
}

const audio_controller_platform_ops_t* audio_controller_host_test_platform_ops(void) {
  static const audio_controller_platform_ops_t ops = {
      NULL,
      host_alloc,
      host_free,
      host_log,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
  };
  return &ops;
}
