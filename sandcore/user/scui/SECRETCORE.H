#ifndef SANDCORE_SECRETCORE_H
#define SANDCORE_SECRETCORE_H
#include "SCTYPES.H"
/* 不分配/复制到String；所有正文操作只触及调用方的专页。 */
typedef struct {unsigned char *data;
    ScU32 maximum,bytes,caret,anchor;
} UiSecretBuffer;
static void ui_secret_wipe(void *p,ScU32 bytes){volatile unsigned char *d=p;
    while(bytes--)*d++=0;
}
static ScU32 ui_secret_next(const UiSecretBuffer *b,ScU32 at)
{if(at>=b->bytes)return b->bytes;
    at++;
    while(at<b->bytes && (b->data[at]&0xc0u)==0x80u)at++;
    return at;
}
static ScU32 ui_secret_previous(const UiSecretBuffer *b,ScU32 at)
{if(!at)return 0;
    at--;
    while(at && (b->data[at]&0xc0u)==0x80u)at--;
    return at;
}
static ScU32 ui_secret_characters(const UiSecretBuffer *b,ScU32 end)
{ScU32 count=0;
    for(ScU32 at=0;at<end;at=ui_secret_next(b,at))count++;
    return count;
}
static void ui_secret_buffer_clear(UiSecretBuffer *b)
{ui_secret_wipe(b->data,b->maximum+1u);
    b->bytes=b->caret=b->anchor=0;
}
static void ui_secret_remove(UiSecretBuffer *b,ScU32 start,ScU32 end)
{
    if(start>end || end>b->bytes)return;
    ScU32 old=b->bytes;
    for(ScU32 i=end;i<old;i++)b->data[start+i-end]=b->data[i];
    b->bytes=old-(end-start);
    /* 删除的后缀即刻擦除，不能只写一个NUL而把旧口令留在同一专页。 */
    ui_secret_wipe(b->data+b->bytes,old-b->bytes+1u);
    b->caret=b->anchor=start;
}
static ScU32 ui_secret_encode(ScU32 scalar,unsigned char out[4])
{
    if(!scalar || scalar>0x10ffffu || (scalar>=0xd800u && scalar<=0xdfffu))return 0;
    if(scalar<0x80u){out[0]=(unsigned char)scalar;
        return 1;
    }
    if(scalar<0x800u){out[0]=(unsigned char)(0xc0u|(scalar>>6));
        out[1]=(unsigned char)(0x80u|(scalar&63u));
        return 2;
    }
    if(scalar<0x10000u){out[0]=(unsigned char)(0xe0u|(scalar>>12));
        out[1]=(unsigned char)(0x80u|((scalar>>6)&63u));
        out[2]=(unsigned char)(0x80u|(scalar&63u));
        return 3;
    }
    out[0]=(unsigned char)(0xf0u|(scalar>>18));
    out[1]=(unsigned char)(0x80u|((scalar>>12)&63u));
    out[2]=(unsigned char)(0x80u|((scalar>>6)&63u));
    out[3]=(unsigned char)(0x80u|(scalar&63u));
    return 4;
}
static int ui_secret_insert(UiSecretBuffer *b,ScU32 scalar)
{
    unsigned char encoded[4]={0};
    ScU32 bytes=ui_secret_encode(scalar,encoded),start=b->caret<b->anchor?b->caret:b->anchor,end=b->caret>b->anchor?b->caret:b->anchor;
    if(!bytes || bytes>b->maximum-(b->bytes-(end-start))){ui_secret_wipe(encoded,sizeof(encoded));
        return 0;
    }
    ui_secret_remove(b,start,end);
    for(ScU32 i=b->bytes;i>start;i--)b->data[i+bytes-1u]=b->data[i-1u];
    for(ScU32 i=0;i<bytes;i++)b->data[start+i]=encoded[i];
    b->bytes+=bytes;
    b->data[b->bytes]=0;
    b->caret=b->anchor=start+bytes;
    ui_secret_wipe(encoded,sizeof(encoded));
    return 1;
}
#endif
