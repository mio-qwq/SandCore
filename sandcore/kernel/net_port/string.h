#ifndef SANDCORE_LWIP_STRING_H
#define SANDCORE_LWIP_STRING_H
#include "port.h"
#define memcpy sc_net_memcpy
#define memmove sc_net_memmove
#define memset sc_net_memset
#define memcmp sc_net_memcmp
#define strlen sc_net_strlen
#define strcmp sc_net_strcmp
#define strncmp sc_net_strncmp
#define strchr sc_net_strchr
#define strstr sc_net_strstr
#endif
