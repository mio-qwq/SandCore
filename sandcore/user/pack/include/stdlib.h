#pragma once
#include <stddef.h>
void *sand_pack_malloc(size_t bytes);
void sand_pack_free(void *address);
#define malloc sand_pack_malloc
#define free sand_pack_free
