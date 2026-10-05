#pragma once
typedef unsigned int size_t;
typedef signed int ptrdiff_t;
#ifndef NULL
#define NULL ((void *)0)
#endif
#define offsetof(t,m) __builtin_offsetof(t,m)
