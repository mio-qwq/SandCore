#ifndef SCCLIB_STDARG_H
#define SCCLIB_STDARG_H
#ifdef __SCCC__
#ifndef __SCCC_VA32__
#error This SDK needs the real SCCC variadic builtins
#endif
typedef unsigned char *va_list;
#else
typedef __builtin_va_list va_list;
#endif
/* 宏交给实际编译器，不根据局部变量地址猜下一参数。 */
#define va_start(ap,last) __builtin_va_start(ap,last)
#define va_arg(ap,type) __builtin_va_arg(ap,type)
#define va_copy(dest,source) __builtin_va_copy(dest,source)
#define va_end(ap) __builtin_va_end(ap)
#endif

