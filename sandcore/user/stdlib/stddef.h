#ifndef SCCLIB_STDDEF_H
#define SCCLIB_STDDEF_H
/* 正式SDK固定为i386；尺寸类型不是宿主编译进程的指针宽度。 */
#if !defined(__i386__) && !defined(__SCCC__)
#error SCCLIB SDK requires an i386 target
#endif
#ifndef SCCLIB_SIZE_T_DEFINED
#define SCCLIB_SIZE_T_DEFINED 1
typedef unsigned int size_t;
#endif
#ifndef SCCLIB_PTRDIFF_T_DEFINED
#define SCCLIB_PTRDIFF_T_DEFINED 1
typedef int ptrdiff_t;
#endif
#ifndef SCCLIB_WCHAR_T_DEFINED
#define SCCLIB_WCHAR_T_DEFINED 1
typedef int wchar_t;
#endif
#define NULL ((void *)0)
#define offsetof(type,member) __builtin_offsetof(type,member)
#endif

