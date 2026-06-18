#ifndef AUDIO_CONTROLLER_INTERNAL_H_
#define AUDIO_CONTROLLER_INTERNAL_H_

#include "audio_controller/audio_controller.h"
#include "topology_parser.h"

struct audio_controller {
  audio_controller_platform_ops_t platform;
  ac_allocator_t allocator;
  ac_topology_t topology;
  char last_error[256];
  int verbose;
};

void ac_set_error(audio_controller_t* controller, const char* fmt, ...);

#endif
