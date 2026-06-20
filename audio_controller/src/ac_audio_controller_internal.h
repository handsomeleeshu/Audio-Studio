#ifndef AC_AUDIO_CONTROLLER_INTERNAL_H_
#define AC_AUDIO_CONTROLLER_INTERNAL_H_

#include "audio_controller.h"
#include "ac_topology_parser.h"

struct audio_controller {
    audio_controller_driver_ops_t driver;
    ac_allocator_t allocator;
    ac_topology_t topology;
    char last_error[256];
    int verbose;
};

/* Store the API-visible last_error and mirror it through driver.log. */
void ac_report_error(audio_controller_t* controller, const char* message);
void ac_report_error_detail(audio_controller_t* controller,
                            const char* message,
                            const char* detail);
void ac_report_error_int(audio_controller_t* controller,
                         const char* message,
                         int value);

#endif
