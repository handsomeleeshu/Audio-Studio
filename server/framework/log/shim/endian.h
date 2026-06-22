#ifndef AUDIO_STUDIO_LOG_SHIM_ENDIAN_H
#define AUDIO_STUDIO_LOG_SHIM_ENDIAN_H

#include <stdint.h>

#ifndef __ORDER_LITTLE_ENDIAN__
#define __ORDER_LITTLE_ENDIAN__ 1234
#endif

#ifndef __ORDER_BIG_ENDIAN__
#define __ORDER_BIG_ENDIAN__ 4321
#endif

#ifndef __BYTE_ORDER__
#if defined(_WIN32) || defined(__LITTLE_ENDIAN__) || defined(__i386__) || \
    defined(__x86_64__) || defined(__aarch64__) || defined(__riscv)
#define __BYTE_ORDER__ __ORDER_LITTLE_ENDIAN__
#else
#define __BYTE_ORDER__ __ORDER_BIG_ENDIAN__
#endif
#endif

#ifndef htobe16
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define htobe16(x) ((uint16_t)__builtin_bswap16((uint16_t)(x)))
#else
#define htobe16(x) ((uint16_t)(x))
#endif
#endif

#ifndef htobe32
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define htobe32(x) ((uint32_t)__builtin_bswap32((uint32_t)(x)))
#else
#define htobe32(x) ((uint32_t)(x))
#endif
#endif

#endif
