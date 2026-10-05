#ifndef SANDCORE_CODEC_STDLIB_H
#define SANDCORE_CODEC_STDLIB_H
#include <stddef.h>
/* mio：仅三环解码服务的兼容声明，不链接宿主libc；实现由
 * codec/runtime.c提供，所有内存最终属于服务任务的私有图形堆。 */
void *malloc(size_t size);
void *calloc(size_t count,size_t size);
void *realloc(void *ptr,size_t size);
void free(void *ptr);
int abs(int value);
void abort(void) __attribute__((noreturn));
#endif
