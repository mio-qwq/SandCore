#include "GFXPRIVATE.H"

ScU32 ScGfxArgbOver(ScU32 source,ScU32 destination)
{
    ScU32 a=source>>24,inverse=255u-a,old=destination>>24,numerator=a*255u+old*inverse;
    if(!a)return destination;
    if(a==255u)return source;
    if(!numerator)return 0;
    ScU32 result=((numerator+127u)/255u)<<24;
    for(ScU32 shift=0;shift<24;shift+=8){ScU32 value=(((source>>shift)&255u)*a*255u+((destination>>shift)&255u)*old*inverse+numerator/2u)/numerator;
        result|=value<<shift;
    }
    return result;
}
ScU32 ScGfxArgbErase(ScU32 destination,ScU32 coverage)
{if(coverage>255u)coverage=255u;
    ScU32 a=((destination>>24)*(255u-coverage)+127u)/255u;
    return a?(destination&0xffffffu)|(a<<24):0;
}
ScStatus ScGfxSample(const ScGfxPixels *p,ScI32 x,ScI32 y,ScU32 filter,ScU32 *out)
{
    if(!gfx_input(p,sizeof(*p)) || !out || !p->pixels || !p->width || !p->height || filter>1u
    || p->width>SC_GFX_MAX_WIDTH || p->height>SC_GFX_MAX_HEIGHT || p->stride<p->width*4u
    || p->height-1u>(UINT_MAX-p->width*4u)/p->stride || (p->height-1u)*p->stride+p->width*4u>p->bytes)return SC_GFX_E_INVALID;
    if(x<0)x=0;
    if(y<0)y=0;
    if((ScU32)x>(p->width-1u)*256u)x=(ScI32)((p->width-1u)*256u);
    if((ScU32)y>(p->height-1u)*256u)y=(ScI32)((p->height-1u)*256u);
    ScU32 xx=(ScU32)x>>8,yy=(ScU32)y>>8;
    const ScU32 *row=(const ScU32 *)((const unsigned char *)p->pixels+yy*p->stride);
    if(!filter){*out=row[xx];
        return 0;
    }ScU32 x1=xx+1u<p->width?xx+1u:xx,y1=yy+1u<p->height?yy+1u:yy;
    const ScU32 *next=(const ScU32 *)((const unsigned char *)p->pixels+y1*p->stride);
    ScU32 wx=(ScU32)x&255u,wy=(ScU32)y&255u;
    ScU32 weights[4]={(256u-wx)*(256u-wy),wx*(256u-wy),(256u-wx)*wy,wx*wy},samples[4]={row[xx],row[x1],next[xx],next[x1]};
    ScU32 alpha=0,red=0,green=0,blue=0;
    /* 权重总和65536，alpha*通道*权重总和<=4261478400，含
     * 四舍五入仍不越u32。先alpha加权，透明RGB不产生蓝/黑边。 */
    for(ScU32 i=0;i<4;i++){ScU32 w=weights[i]*(samples[i]>>24);
        alpha+=w;
        red+=w*((samples[i]>>16)&255u);
        green+=w*((samples[i]>>8)&255u);
        blue+=w*(samples[i]&255u);
    }
     *out=alpha?(((alpha+32768u)>>16)<<24)|(((red+alpha/2u)/alpha)<<16)|(((green+alpha/2u)/alpha)<<8)|((blue+alpha/2u)/alpha):0;
    return 0;
}
