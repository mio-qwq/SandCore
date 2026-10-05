#pragma once
#include <stddef.h>
void *sand_pack_copy(void *to,const void *from,size_t bytes);
void *sand_pack_fill(void *to,int value,size_t bytes);
#define memcpy sand_pack_copy
#define memset sand_pack_fill
