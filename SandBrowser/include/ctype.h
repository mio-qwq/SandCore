/* Copyright (c) 2026 mio. 直接使用 SCCLIB 的 C locale 函数。 */
#ifndef SB_CTYPE_H
#define SB_CTYPE_H
#ifdef __cplusplus
/* libstdc++ 的 ctype_base 类型声明需要这些位值；字符判断仍调用 SCCLIB。 */
enum {
    _ISupper = 0x100,
    _ISlower = 0x200,
    _ISalpha = 0x400,
    _ISdigit = 0x800,
    _ISxdigit = 0x1000,
    _ISspace = 0x2000,
    _ISprint = 0x4000,
    _ISgraph = 0x8000,
    _ISblank = 1,
    _IScntrl = 2,
    _ISpunct = 4,
    _ISalnum = 8
};
extern "C" {
#endif
int isalnum(int);
int isalpha(int);
int isblank(int);
int iscntrl(int);
int isdigit(int);
int isgraph(int);
int islower(int);
int isprint(int);
int ispunct(int);
int isspace(int);
int isupper(int);
int isxdigit(int);
int tolower(int);
int toupper(int);
#ifdef __cplusplus
}
#endif
#endif
