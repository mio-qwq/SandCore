#pragma once
/* 固定i386类型，仅供压缩核心私有编译链；不发布为标准运行库。 */
typedef unsigned int size_t;
typedef signed int ptrdiff_t;
#define NULL ((void *)0)
#define offsetof(t,m) __builtin_offsetof(t,m)
