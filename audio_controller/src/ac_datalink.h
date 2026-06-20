#ifndef AC_DATALINK_H_
#define AC_DATALINK_H_

#include "audio_controller.h"

#define AC_DATALINK_MAX_FRAME_SIZE 1024u
#define AC_DATALINK_MAX_PACKET_SIZE 4096u
#define AC_DATALINK_RX_STREAM_SIZE (AC_DATALINK_MAX_FRAME_SIZE * 2u)

typedef struct ac_datalink_controller {
    const audio_controller_datalink_device_ops_t* ops;
    int open;
    size_t mtu;
    unsigned char rx_stream[AC_DATALINK_RX_STREAM_SIZE];
    size_t rx_stream_size;
    uint32_t next_link_sequence;
    uint32_t tx_packets;
    uint32_t rx_packets;
    uint32_t retries;
} ac_datalink_controller_t;

int ac_datalink_init(ac_datalink_controller_t* datalink,
                     const audio_controller_datalink_device_ops_t* ops);
void ac_datalink_deinit(ac_datalink_controller_t* datalink);
int ac_datalink_send_packet(ac_datalink_controller_t* datalink,
                            const unsigned char* payload,
                            size_t payload_size,
                            unsigned int timeout_ms);
int ac_datalink_receive_packet(ac_datalink_controller_t* datalink,
                               unsigned char* payload,
                               size_t capacity,
                               size_t* actual_size,
                               unsigned int timeout_ms);
int ac_datalink_poll(ac_datalink_controller_t* datalink,
                     unsigned int timeout_ms);

#endif
