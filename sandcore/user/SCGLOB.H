#ifndef SCGLOB_H
#define SCGLOB_H
#include "SCIO.H"
/* 路径段/文件选择共享的自写通配符。迭代推进，不递归展开星号；
 * 字节语义与C locale相同，大小写默认区分，权限不依赖匹配结果。 */
static int glob_unit(const char **pattern,u8 c)
{
    const char *p=*pattern;int yes=0;
    if(*p=='?'){*pattern=p+1;return 1;}if(*p=='\\' && p[1]){*pattern=p+2;return (u8)p[1]==c;}
    if(*p!='['){*pattern=p+(*p?1:0);return *p && (u8)*p==c;}
    const char *start=p++;int invert=*p=='!' || *p=='^';if(invert)p++;int first=1;
    while(*p && (*p!=']' || first)){u8 lo=(u8)*p++;if(lo=='\\' && *p)lo=(u8)*p++;
        u8 hi=lo;if(*p=='-' && p[1] && p[1]!=']'){p++;hi=(u8)*p++;if(hi=='\\' && *p)hi=(u8)*p++;}
        if(c>=lo && c<=hi)yes=1;first=0;}
    if(*p!=']'){*pattern=start+1;return c=='[';}*pattern=p+1;return invert?!yes:yes;
}
static int glob_match(const char *text,const char *pattern)
{
    const char *star=0,*retry=0;
    while(*text){if(*pattern=='*'){do{pattern++;}while(*pattern=='*');star=pattern;retry=text;continue;}
        const char *next=pattern;if(glob_unit(&next,(u8)*text)){pattern=next;text++;continue;}
        if(!star)return 0;pattern=star;text=++retry;}
    while(*pattern=='*')pattern++;return !*pattern;
}
#endif
