#ifndef SANDCORE_CODEC_STDINT_H
#define SANDCORE_CODEC_STDINT_H
/* mio：本文件只描述固定的i386 ABI。不能让32位WSL GCC转去宿主
 * glibc的bits头文件，也不让MinGW默认long大小影响压缩位流。 */
typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef signed short int16_t;
typedef unsigned short uint16_t;
typedef signed int int32_t;
typedef unsigned int uint32_t;
typedef signed long long int64_t;
typedef unsigned long long uint64_t;
typedef int intptr_t;
typedef unsigned int uintptr_t;
#define UINT8_MAX 255
#define UINT16_MAX 65535
#define UINT32_MAX 0xFFFFFFFFu
#define UINT64_MAX 0xFFFFFFFFFFFFFFFFull
#define INT8_MAX 127
#define INT16_MAX 32767
#define INT32_MAX 2147483647
#define INT32_MIN (-2147483647-1)
#define INT64_MAX 0x7FFFFFFFFFFFFFFFll
#define INT64_MIN (-INT64_MAX-1)
#define SIZE_MAX UINT32_MAX
#define UINT32_C(value) value##u
#define UINT64_C(value) value##ull
#define INT32_C(value) value
#define INT64_C(value) value##ll
#endif
