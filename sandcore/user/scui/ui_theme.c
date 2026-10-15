static ScStatus ui_glyph_load(const ScView *key,const ScCacheStamp *stamp,void *context,void **out,ScU32 *cost)
{
    UiGlyphKey k;
    ScFontGlyphDesc d;
    ScFontGlyph *g;
    ScStatus s;
    (void)stamp;
    (void)context;
    if(key->bytes!=sizeof(k))return SC_UI_E_INVALID;
    memcpy(&k,key->data,sizeof(k));
    g=(ScFontGlyph *)malloc(sizeof(*g));
    if(!g)return SC_UI_E_MEMORY;
    memset(&d,0,sizeof(d));
    d.size=sizeof(d);
    d.version=1;
    d.face=k.face;
    d.scalar=k.scalar;
    d.pixels=k.pixels;
    d.expected_generation=k.generation;
    memset(g,0,sizeof(*g));
    g->size=sizeof(*g);
    g->version=1;
    s=ScFontGetGlyph(&d,g);
    if(s<0){free(g);
        return s;
        }
    *out=g;
    *cost=sizeof(*g);
    return SC_UI_OK;
}
static void ui_glyph_drop(void *resource,void *context){(void)context;
    free(resource);
    }
static ScStatus ui_theme_refresh(ScUiContext *c)
{
    ScThemeInfo theme;
    ScDisplayInfo display;
    ScFontInfo fonts[2];
    ScStatus s;
    ScU32 i;
    memset(&theme,0,sizeof(theme));
    theme.size=sizeof(theme);
    theme.version=1;
    s=ScThemeQuery(&theme);
    if(s<0)return ui_cause(c,s,UI_OP_FONT,0);
    memset(&display,0,sizeof(display));
    display.size=sizeof(display);
    display.version=1;
    s=ScDisplayQuery(&display);
    if(s<0)return ui_cause(c,s,UI_OP_FONT,0);
    for(i=0;i<2;i++){memset(&fonts[i],0,sizeof(fonts[i]));
        fonts[i].size=sizeof(fonts[i]);
        fonts[i].version=1;
        s=ScFontQuery(i?16:12,&fonts[i]);
        if(s<0)return ui_cause(c,s,UI_OP_FONT,0);
        }
    if(!fonts[c->description.default_font_face==12?0:1].active)return ui_fail(c,SC_UI_E_NOT_SUPPORTED,SC32_E_NOT_FOUND,SC32_DOMAIN_KERNEL,UI_OP_FONT,0);
    if(!c->glyphs){ScCacheDesc d;
        memset(&d,0,sizeof(d));
        d.size=sizeof(d);
        d.version=1;
        d.budget_bytes=c->description.cache_budget_bytes;
        d.load=ui_glyph_load;
        d.drop=ui_glyph_drop;
        d.context=c;
        s=ScCacheCreate(&d,&c->glyphs);
        if(s<0)return ui_cause(c,s,UI_OP_FONT,0);
        }
    c->theme=theme;
    c->display=display;
    c->fonts[0]=fonts[0];
    c->fonts[1]=fonts[1];
    return SC_UI_OK;
}
static ScU32 ui_color(ScUiContext *c,ScU32 role)
{return 0xff000000u|c->theme.colors[role<24?role:SC32_COLOR_TEXT];
    }
static ScI32 ui_scale(ScUiContext *c,ScI32 logical)
{
    ScU32 scale=c->description.scale_mode?c->description.scale_percent:c->display.scale_percent;
    ScU32 magnitude=logical<0?0u-(ScU32)logical:(ScU32)logical,whole=magnitude/100u,remainder=magnitude%100u,value;
    if(!scale)scale=100;
    if(whole>(ScU32)INT_MAX/scale)return logical<0?-INT_MAX:INT_MAX;
    value=whole*scale+(remainder*scale)/100u;
    if(value>(ScU32)INT_MAX)return logical<0?-INT_MAX:INT_MAX;
    return logical<0?-(ScI32)value:(ScI32)value;
}
static void ui_font_role(ScUiContext *c,ScU32 role,ScU32 *face,ScU32 *pixels,ScFontInfo **info)
{
    ScU32 base=c->description.default_font_pixels;
    *face=c->description.default_font_face;
    if(role==SC_UI_FONT_DENSE && c->fonts[0].active){*face=12;
        base=12;
        }
    else if(role==SC_UI_FONT_HEADING && base<=60)base+=4;
    *pixels=(ScU32)ui_scale(c,(ScI32)base);
    *info=&c->fonts[*face==12?0:1];
}
static ScStatus ui_glyph(ScUiContext *c,ScU32 role,ScU32 scalar,const ScFontGlyph **out,ScU32 *lease)
{
    UiGlyphKey k;
    ScCacheStamp stamp;
    ScFontInfo *info;
    void *resource=0;
    ScStatus s;
    ui_font_role(c,role,&k.face,&k.pixels,&info);
    k.scalar=scalar;
    k.generation=info->generation;
    if(!k.pixels || k.pixels>64 || !info->active)return ui_fail(c,SC_UI_E_RANGE,SC32_E_RANGE,SC32_DOMAIN_KERNEL,UI_OP_FONT,0);
    memset(&stamp,0,sizeof(stamp));
    stamp.size=sizeof(stamp);
    stamp.version=1;
    stamp.namespace_id=UI_OP_FONT;
    stamp.object_id=scalar;
    stamp.object_generation=k.generation;
    stamp.user_generation=k.pixels;
    s=ScCacheAcquire(c->glyphs,&k,sizeof(k),&stamp,&resource,lease);
    if(s<0)return ui_cause(c,s,UI_OP_FONT,0);
    *out=(const ScFontGlyph *)resource;
    return SC_UI_OK;
}
static void ui_glyph_release(ScUiContext *c,ScU32 lease){ScCacheReleaseLease(c->glyphs,lease);
    }
static void ui_font_line(ScUiContext *c,ScU32 role,ScU32 *height,ScU32 *baseline)
{
    ScFontInfo *f;
    ScU32 face,pixels;
    ui_font_role(c,role,&face,&pixels,&f);
    (void)face;
    *baseline=((ScU32)f->ascent*pixels+f->units_per_em-1)/f->units_per_em;
    *height=*baseline+((ScU32)(-f->descent)*pixels+f->units_per_em-1)/f->units_per_em
        +((ScU32)f->line_gap*pixels+f->units_per_em-1)/f->units_per_em+2;
}
static ScStatus ui_text_measure(ScUiContext *c,ScU32 role,const ScUiText *text,ScI32 width,ScU32 flags,ScUiTextMetrics *out)
{
    ScUiTextMetrics v;
    ScU32 pos=0,next,scalar,x=0,max=0,line,baseline,missing=0;
    ScStatus s;
    s=ui_input(text,sizeof(*text));
    if(s<0)return s;
    if(role>SC_UI_FONT_CODE || width<SC_UI_AUTO || flags&~(UI_TEXT_WRAP|UI_TEXT_ELLIPSIS|UI_TEXT_CENTER|UI_TEXT_RIGHT|UI_TEXT_UNDERLINE|UI_TEXT_STRIKE))return SC_UI_E_INVALID;
    s=ScUtf8Validate(text->data,text->bytes,&next);
    if(s<0)return SC_UI_E_UTF8;
    ui_font_line(c,role,&line,&baseline);
    memset(&v,0,sizeof(v));
    v.size=sizeof(v);
    v.version=1;
    v.lines=1;
    v.baseline=baseline;
    while(pos<text->bytes){ScU32 end,cluster_width=0,walk=pos;
        s=ui_grapheme_next(text->data,text->bytes,pos,&end);
        if(s<0)return s;
        if(end<=pos || end>text->bytes)return SC_UI_E_FAILED;
        while(walk<end){const ScFontGlyph *g;
            ScU32 lease=0;
            next=walk;
            s=ScUtf8Decode(text->data,text->bytes,&next,&scalar);
            if(s<0)return SC_UI_E_UTF8;
            walk=next;
            if(scalar=='\r' || scalar=='\n')continue;
            if(scalar=='\t'){ScU32 cell=(ScU32)ui_scale(c,8)*4;
                cluster_width=cell-(x%cell);
                continue;
                }
            s=ui_glyph(c,role,scalar,&g,&lease);
            if(s<0)return s;
            if(!g->mapped)missing++;
            if(g->advance<0 || !ui_add(cluster_width,(ScU32)g->advance,&cluster_width)){ui_glyph_release(c,lease);
                return SC_UI_E_RANGE;
                }
            ui_glyph_release(c,lease);
        }
        if(((const unsigned char *)text->data)[pos]=='\r' || ((const unsigned char *)text->data)[pos]=='\n'){
            if(x>max)max=x;
            x=0;
            v.lines++;
        }
        else{
            if((flags&UI_TEXT_WRAP) && width>=0 && x && (x>(ScU32)width || cluster_width>(ScU32)width-x)){if(x>max)max=x;
                x=0;
                v.lines++;
                }
            if(!ui_add(x,cluster_width,&x))return SC_UI_E_RANGE;
        }
        pos=end;
    }
    if(x>max)max=x;
    if(!ui_multiply(v.lines,line,&v.height))return SC_UI_E_RANGE;
    v.width=max;
    v.generation=c->fonts[c->description.default_font_face==12?0:1].generation;
    v.missing_glyphs=missing;
    *out=v;
    return SC_UI_OK;
}
ScStatus ScUiMeasureText(ScUiContext *c,ScU32 role,const ScUiText *text,ScI32 width,ScU32 flags,ScUiTextMetrics *out)
{
    ScUiTextMetrics v;
    ScStatus s=ui_output(out,sizeof(*out));
    if(s<0)return s;
    s=ui_object_enter((UiObject *)c,UI_CONTEXT);
    if(s<0)return s;
    s=ui_text_measure(c,role,text,width,flags,&v);
    if(s==0)memcpy(out,&v,sizeof(v));
    s=ui_cause(c,s,UI_OP_FONT,0);
    ui_object_leave(&c->object);
    return s;
}
ScStatus ScUiMetricsQuery(ScUiContext *c,ScUiNode root,ScUiMetrics *out)
{
    ScUiMetrics v;
    ScFontInfo *f;
    ScU32 pixels,face,line,baseline;
    ScStatus s=ui_output(out,sizeof(*out));
    if(s<0)return s;
    if(!c || c->object.kind!=UI_CONTEXT || c->object.closing || (root && !ui_root(c,root)))return SC_UI_E_STALE;
    memset(&v,0,sizeof(v));
    v.size=sizeof(v);
    v.version=1;
    v.scale_percent=c->description.scale_mode?c->description.scale_percent:c->display.scale_percent;
    ui_font_role(c,SC_UI_FONT_BODY,&face,&pixels,&f);
    ui_font_line(c,SC_UI_FONT_BODY,&line,&baseline);
    (void)baseline;
    v.font_face=face;
    v.font_pixels=pixels;
    v.font_epoch=f->generation;
    v.theme_epoch=c->theme.generation;
    v.display_epoch=c->display.generation;
    v.unit=(ScU32)ui_scale(c,1);
    v.row_height=line+(ScU32)ui_scale(c,2);
    v.menu_height=(ScU32)ui_scale(c,24);
    if(v.menu_height<v.row_height)v.menu_height=v.row_height;
    v.toolbar_height=(ScU32)ui_scale(c,32);
    v.status_height=(ScU32)ui_scale(c,24);
    v.gap=(ScU32)ui_scale(c,6);
    v.splitter=(ScU32)ui_scale(c,4);
    v.border=(ScU32)ui_scale(c,1);
    memcpy(out,&v,sizeof(v));
    return SC_UI_OK;
}
