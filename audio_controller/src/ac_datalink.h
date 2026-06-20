#ifndef AC_DATALINK_H_
#define AC_DATALINK_H_

#include "audio_controller.h"

typedef struct ac_datalink_controller {
    const audio_controller_datalink_device_ops_t* ops;
    int open;
    size_t mtu;
    uint32_t tx_packets;
    uint32_t rx_packets;
    uint32_t retries;
} ac_datalink_controller_t;

int ac_datalink_init(ac_datalink_controller_t* datalink,
                     const audio_controller_datalink_device_ops_t* ops);
void ac_datalink_deinit(ac_datalink_controller_t* datalink);
int ac_datalink_poll(ac_datalink_controller_t* datalink,
                     unsigned int timeout_ms);

#endif
