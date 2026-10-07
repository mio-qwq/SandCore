#ifndef SANDCORE_LWIP_CC_H
#define SANDCORE_LWIP_CC_H
#include "port.h"
#define BYTE_ORDER LITTLE_ENDIAN
#define LWIP_NO_STDINT_H 1
#define LWIP_NO_INTTYPES_H 1
#define LWIP_NO_CTYPE_H 1
#define LWIP_NO_UNISTD_H 1
#define LWIP_NO_LIMITS_H 1
#define INT_MAX 2147483647
typedef unsigned char u8_t;
typedef signed char s8_t;
typedef unsigned short u16_t;
typedef signed short s16_t;
typedef unsigned int u32_t;
typedef signed int s32_t;
typedef unsigned int mem_ptr_t;
#define LWIP_HAVE_INT64 0
#define U16_F "u"
#define S16_F "d"
#define X16_F "x"
#define U32_F "u"
#define S32_F "d"
#define X32_F "x"
#define X8_F "x"
#define SZT_F "u"
#define LWIP_PLATFORM_DIAG(x) do {} while(0)
#define LWIP_PLATFORM_ASSERT(x) sc_net_assert(x)
#define LWIP_RAND() sc_net_rand()
#define LWIP_ASSERT_CORE_LOCKED() sc_net_core_assert()
#define PACK_STRUCT_STRUCT __attribute__((packed))
#endif
