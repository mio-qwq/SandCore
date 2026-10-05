#ifndef SCUU_H
#define SCUU_H
#include "SCTEXT.H"
static const char *uu_base64="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static char uu_char(u32 n){n&=63;return n?(char)(n+32):'`';}
static int uu_safe_name(const char *p)
{if(!*p || *p=='/' || *p=='\\' || length(p)>63)return 0;const char *part=p;
    for(const char *at=p;;at++){if(*at=='/' || !*at){if(at==part || (at-part==1 && *part=='.') || (at-part==2 && part[0]=='.' && part[1]=='.'))return 0;if(!*at)break;part=at+1;}
        else if((u8)*at<32 || *at=='\\')return 0;}return 1;}
static int uu_b64_value(char c)
{for(int i=0;i<64;i++)if(uu_base64[i]==c)return i;return -1;}
#endif
