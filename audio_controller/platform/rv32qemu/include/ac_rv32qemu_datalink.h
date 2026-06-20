#ifndef AC_RV32QEMU_DATALINK_H_
#define AC_RV32QEMU_DATALINK_H_

#include <stddef.h>

#include "audio_controller.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ac_rv32qemu_datalink {
    const char* rx_path;
    const char* tx_path;
    size_t mtu;
    size_t rx_offset;
    int open;
    audio_controller_datalink_device_ops_t ops;
} ac_rv32qemu_datalink_t;

void ac_rv32qemu_datalink_init(ac_rv32qemu_datalink_t* datalink,
                               const char* rx_path,
                               const char* tx_path,
                               size_t mtu);

const audio_controller_datalink_device_ops_t*
ac_rv32qemu_datalink_ops(ac_rv32qemu_datalink_t* datalink);

#ifdef __cplusplus
}
#endif

#endif
