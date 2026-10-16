#include "../SCUIARGB.H"

ScStatus ScUiPainterArgb(ScUiPainter *p,const ScUiArgbBlit *d)
{
    ScStatus s=ui_input(d,sizeof(*d));
    if(s<0 || !p || !p->root || !p->node)return s<0?s:SC_UI_E_INVALID;
    if(!ui_root_can_paint(p->root))return SC_UI_E_STALE;
    if(d->flags>SC_UI_ARGB_COPY || d->reserved[0] || d->reserved[1] || d->reserved[2] || !d->pixels
    || !d->width || !d->height || d->width>UINT_MAX/4u || d->stride<d->width*4u
    || d->height-1u>(UINT_MAX-d->width*4u)/d->stride
    || (d->height-1u)*d->stride+d->width*4u>d->bytes)return SC_UI_E_RANGE;
    if(d->source.x<0 || d->source.y<0 || d->source.width<0 || d->source.height<0
    || (ScU32)d->source.x>d->width || (ScU32)d->source.y>d->height
    || (ScU32)d->source.width>d->width-(ScU32)d->source.x || (ScU32)d->source.height>d->height-(ScU32)d->source.y
    || d->destination.width!=d->source.width || d->destination.height!=d->source.height)return SC_UI_E_RANGE;
    ScUiRect clipped;
    if(!ui_painter_rect(p,&d->destination,&clipped))return p->error;
    /* 起点始终由原destination反推，不能拿裁后宽度当比例分母。
     * 这里只做1:1物理blit；DPI与任意倍率在拥有缓存的Canvas中完成。 */
    ScU32 sx=(ScU32)d->source.x+(ScU32)clipped.x-(ScU32)p->origin_x-(ScU32)d->destination.x;
    ScU32 sy=(ScU32)d->source.y+(ScU32)clipped.y-(ScU32)p->origin_y-(ScU32)d->destination.y;
    for(ScI32 y=0;y<clipped.height;y++){
        const ScU32 *source=(const ScU32 *)((const unsigned char *)d->pixels+(sy+(ScU32)y)*d->stride)+sx;
        ScU32 *target=p->root->pixels+((ScU32)clipped.y+(ScU32)y)*p->root->width+(ScU32)clipped.x;
        for(ScI32 x=0;x<clipped.width;x++){ScU32 color=source[x],alpha=color>>24;
            if(d->flags==SC_UI_ARGB_COPY || alpha==255u)target[x]=color;
            else if(alpha){ScU32 old=target[x],value=0xff000000u;
                for(ScU32 shift=0;shift<24;shift+=8)value|=(((((color>>shift)&255u)*alpha+((old>>shift)&255u)*(255u-alpha)+127u)/255u)<<shift);
                target[x]=value;
            }
        }
    }
    return 0;
}
