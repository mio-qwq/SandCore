/* Copyright (c) 2026 mio. i386 cdecl 保存域；没有宿主 OS 依赖。 */
#ifndef SB_SETJMP_H
#define SB_SETJMP_H
typedef unsigned int jmp_buf[6];
#ifdef __cplusplus
extern "C" {
#endif
int setjmp(jmp_buf) __attribute__((returns_twice));
int _setjmp(jmp_buf) __attribute__((returns_twice));
void longjmp(jmp_buf, int) __attribute__((noreturn));
void _longjmp(jmp_buf, int) __attribute__((noreturn));
#ifdef __cplusplus
}
#endif
#endif
