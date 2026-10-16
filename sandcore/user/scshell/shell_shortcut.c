#include <SC32PALETTE.H>
static void sh_shortcut_dispose(ShObject *base) {
    ScShellShortcut *s=(ScShellShortcut *)base;
    ScU32 i;
    for(i=0;i<5;++i)free(s->strings[i]);
    ScBufferRelease(s->legacy);
}
ScStatus ScShellShortcutCreate(const ScShellShortcutDesc *d,ScShellShortcut **out) {
    ScShellShortcut *s;
    const char *src[5];
    ScU32 max[5]= {
        1023,255,127,1023,127
    },i;
    ScStatus status=sh_record(d,sizeof(*d));
    if(status<0)return status;
    if(!out||d->kind<1||d->kind>4||d->flags||d->reserved[0]||d->reserved[1]|| ((d->kind==SC_SHELL_SHORTCUT_PATH||d->kind==SC_SHELL_SHORTCUT_APPLICATION)?(d->profile!=0&&d->profile!=1&&d->profile!=2):(d->profile!=1&&d->profile!=2)))return SC_SHELL_E_INVALID;
    if(d->kind==SC_SHELL_SHORTCUT_PATH&&d->arguments&&*d->arguments)return SC_SHELL_E_INVALID;
    if(d->kind==SC_SHELL_SHORTCUT_APPLICATION&&!sh_name(d->application))return SC_SHELL_E_INVALID;
    if(d->kind==SC_SHELL_SHORTCUT_APPLICATION&&d->target&&*d->target)return SC_SHELL_E_INVALID;
    src[0]=d->label;
    src[1]=d->application;
    src[2]=d->target;
    src[3]=d->arguments;
    src[4]=d->icon;
    if(d->kind==SC_SHELL_SHORTCUT_LEGACY) {
        ScU32 j;
        max[0]=31;
        max[2]=127;
        max[4]=63;
        if((d->application&&*d->application)||(d->arguments&&*d->arguments))return SC_SHELL_E_INVALID;
        for(j=0;j<5;++j)if(src[j]) {
            ScU32 k;
            for(k=0;src[j][k];++k)if((unsigned char)src[j][k]<32||src[j][k]==127)return SC_SHELL_E_INVALID;
        }
    }
    s=(ScShellShortcut *)sh_new(sizeof(*s),SH_SHORTCUT,sh_shortcut_dispose);
    if(!s)return SC_SHELL_E_MEMORY;
    for(i=0;i<5;++i) {
        if(i==2&&(d->kind==SC_SHELL_SHORTCUT_EXECUTABLE||d->kind==SC_SHELL_SHORTCUT_PATH))status=sh_absolute(src[i],s->strings+i);
        else status=sh_copy_text(src[i],max[i],i==1||i>=3||(i==2&&d->kind==SC_SHELL_SHORTCUT_APPLICATION),s->strings+i);
        if(status<0) {
            sh_release(&s->base);
            return status;
        }
    }
    s->desc=*d;
    s->desc.size=sizeof(s->desc);
    s->desc.version=1;
    s->desc.label=s->strings[0];
    s->desc.application=s->strings[1];
    s->desc.target=s->strings[2];
    s->desc.arguments=s->strings[3];
    s->desc.icon=s->strings[4];
    *out=s;
    return 0;
}
ScStatus ScShellShortcutDescribe(const ScShellShortcut *s,ScShellShortcutDesc *out) {
    if(!sh_valid(s,SH_SHORTCUT))return SC_SHELL_E_STALE;
    return sh_output(out,&s->desc,sizeof(s->desc));
}
void ScShellShortcutRelease(ScShellShortcut *s) {
    if(sh_valid(s,SH_SHORTCUT))sh_release(&s->base);
}
static ScStatus sh_config_string(ScConfig *config,const char *key,const char *text) {
    ScValue v;
    memset(&v,0,sizeof(v));
    v.size=sizeof(v);
    v.version=1;
    v.type=SC_VALUE_UTF8;
    v.data=text;
    v.bytes=(ScU32)strlen(text);
    return sh_cause(ScConfigSet(config,"shortcut",key,&v),61,0);
}
static ScStatus sh_config_number(ScConfig *config,const char *key,ScU32 n) {
    ScValue v;
    memset(&v,0,sizeof(v));
    v.size=sizeof(v);
    v.version=1;
    v.type=SC_VALUE_U32;
    v.lo=n;
    return sh_cause(ScConfigSet(config,"shortcut",key,&v),61,0);
}
ScStatus ScShellShortcutSerialize(const ScShellShortcut *shortcut,ScBuffer **out) {
    ScConfig *config=0;
    ScString *text=0;
    ScBuffer *b=0;
    ScView v;
    ScStatus s;
    if(!sh_valid(shortcut,SH_SHORTCUT))return SC_SHELL_E_STALE;
    if(!out)return SC_SHELL_E_INVALID;
    if(shortcut->legacy)return sh_buffer_copy(shortcut->legacy,out);
    if(shortcut->desc.kind==SC_SHELL_SHORTCUT_LEGACY) {
        s=ScBufferCreate(0,&b);
        if(s>=0)s=ScBufferAppend(b,shortcut->desc.label,(ScU32)strlen(shortcut->desc.label));
        if(s>=0)s=ScBufferAppend(b,"\n",1);
        if(s>=0)s=ScBufferAppend(b,shortcut->desc.target,(ScU32)strlen(shortcut->desc.target));
        if(s>=0)s=ScBufferAppend(b,"\n",1);
        if(s>=0)s=ScBufferAppend(b,shortcut->desc.icon,(ScU32)strlen(shortcut->desc.icon));
        if(s>=0)s=ScBufferAppend(b,"\n",1);
        if(s<0) {
            ScBufferRelease(b);
            return sh_cause(s,61,0);
        }
        *out=b;
        return 0;
    }
    s=ScConfigCreate(&config);
    if(s>=0)s=sh_config_number(config,"version",1);
    if(s>=0)s=sh_config_number(config,"kind",shortcut->desc.kind);
    if(s>=0)s=sh_config_number(config,"profile",shortcut->desc.profile);
    if(s>=0)s=sh_config_string(config,"label",shortcut->desc.label);
    if(s>=0)s=sh_config_string(config,"application",shortcut->desc.application);
    if(s>=0)s=sh_config_string(config,"target",shortcut->desc.target);
    if(s>=0)s=sh_config_string(config,"arguments",shortcut->desc.arguments);
    if(s>=0)s=sh_config_string(config,"icon",shortcut->desc.icon);
    if(s>=0)s=ScConfigSerialize(config,SC_CONFIG_SORT_KEYS,&text);
    if(s>=0) {
        memset(&v,0,sizeof(v));
        v.size=sizeof(v);
        v.version=1;
        s=ScStringView(text,&v);
    }
    if(s>=0)s=ScBufferCreate(v.bytes+8,&b);
    if(s>=0)s=ScBufferAppend(b,"SCLNK2\n",7);
    if(s>=0)s=ScBufferAppend(b,v.data,v.bytes);
    ScConfigRelease(config);
    ScStringRelease(text);
    if(s<0) {
        ScBufferRelease(b);
        return sh_cause(s,61,0);
    }
    *out=b;
    return 0;
}
static ScStatus sh_config_get_string(ScConfig *c,const char *key,char **out) {
    ScValue v;
    ScStatus s;
    memset(&v,0,sizeof(v));
    v.size=sizeof(v);
    v.version=1;
    s=ScConfigGet(c,"shortcut",key,&v);
    if(s<0)return sh_cause(s,62,0);
    if(v.type!=SC_VALUE_UTF8||v.bytes>4095||memchr(v.data,0,v.bytes))return SC_SHELL_E_PROTOCOL;
    *out=(char *)malloc(v.bytes+1);
    if(!*out)return SC_SHELL_E_MEMORY;
    memcpy(*out,v.data,v.bytes);
    (*out)[v.bytes]=0;
    return 0;
}
static ScStatus sh_config_get_number(ScConfig *c,const char *key,ScU32 *out) {
    ScValue v;
    ScStatus s;
    memset(&v,0,sizeof(v));
    v.size=sizeof(v);
    v.version=1;
    s=ScConfigGet(c,"shortcut",key,&v);
    if(s<0)return sh_cause(s,62,0);
    if(v.type!=SC_VALUE_U32)return SC_SHELL_E_PROTOCOL;
    *out=v.lo;
    return 0;
}
ScStatus ScShellShortcutParse(const void *bytes,ScU32 count,const char *base,ScShellShortcut **out) {
    const unsigned char *p=(const unsigned char *)bytes;
    ScShellShortcutDesc d;
    ScShellShortcut *shortcut=0;
    ScConfig *config=0;
    char *strings[5]= {
        0,0,0,0,0
    };
    ScString *joined=0;
    ScStatus s=0;
    ScU32 i;
    if(!out||!p||!count||memchr(p,0,count))return SC_SHELL_E_INVALID;
    memset(&d,0,sizeof(d));
    d.size=sizeof(d);
    d.version=1;
    d.profile=SC_SHELL_PROFILE_MODERN;
    if(count>=7&&!memcmp(p,"SCLNK2\n",7)) {
        ScParseIssue issue;
        ScContainerInfo info;
        ScU32 version;
        memset(&issue,0,sizeof(issue));
        issue.size=sizeof(issue);
        issue.version=1;
        s=ScConfigCreate(&config);
        if(s>=0)s=ScConfigParse(config,p+7,count-7,0,&issue);
        if(s<0)goto done;
        memset(&info,0,sizeof(info));
        info.size=sizeof(info);
        info.version=1;
        s=ScConfigQuery(config,&info);
        if(s<0)goto done;
        if(info.length!=8) {
            s=SC_SHELL_E_PROTOCOL;
            goto done;
        }
        s=sh_config_get_number(config,"version",&version);
        if(s<0)goto done;
        if(version!=1) {
            s=SC_SHELL_E_VERSION;
            goto done;
        }
        s=sh_config_get_number(config,"kind",&d.kind);
        if(s<0)goto done;
        s=sh_config_get_number(config,"profile",&d.profile);
        if(s<0)goto done;
        s=sh_config_get_string(config,"label",strings);
        if(s<0)goto done;
        s=sh_config_get_string(config,"application",strings+1);
        if(s<0)goto done;
        s=sh_config_get_string(config,"target",strings+2);
        if(s<0)goto done;
        s=sh_config_get_string(config,"arguments",strings+3);
        if(s<0)goto done;
        s=sh_config_get_string(config,"icon",strings+4);
        if(s<0)goto done;
        if(d.kind==SC_SHELL_SHORTCUT_LEGACY) {
            s=SC_SHELL_E_PROTOCOL;
            goto done;
        }
    } else {
        ScU32 at=0,limits[3]= {
            31,127,63
        };
        if(count>511) {
            s=SC_SHELL_E_RANGE;
            goto done;
        }
        for(i=0;i<3;++i) {
            ScU32 n=0;
            char line[128];
            while(at<count&&p[at]!='\n') {
                if(p[at]!='\r') {
                    if(n==limits[i]) {
                        s=SC_SHELL_E_PROTOCOL;
                        goto done;
                    }
                    line[n++]=(char)p[at];
                }
                ++at;
            }
            strings[i==0?0:(i==1?2:4)]=(char *)malloc(n+1);
            if(!strings[i==0?0:(i==1?2:4)]) {
                s=SC_SHELL_E_MEMORY;
                goto done;
            }
            memcpy(strings[i==0?0:(i==1?2:4)],line,n);
            strings[i==0?0:(i==1?2:4)][n]=0;
            if(at<count)++at;
        } while(at<count&&(p[at]=='\r'||p[at]=='\n'))++at;
        if(at!=count) {
            s=SC_SHELL_E_PROTOCOL;
            goto done;
        }
        d.kind=SC_SHELL_SHORTCUT_LEGACY;
        d.profile=SC_SHELL_PROFILE_LEGACY;
    }
    d.label=strings[0];
    d.application=strings[1];
    d.target=strings[2];
    d.arguments=strings[3];
    d.icon=strings[4];
    if((d.kind==SC_SHELL_SHORTCUT_EXECUTABLE||d.kind==SC_SHELL_SHORTCUT_PATH)&&d.target&&d.target[0]!='/'&&base) {
        const char *text;
        s=ScPathNormalize(d.target,base,SC_PATH_REQUIRE_ABSOLUTE|SC_PATH_REJECT_ABOVE_ROOT,&joined);
        if(s>=0)s=ScStringCStr(joined,&text);
        if(s<0)goto done;
        d.target=text;
    }
    s=ScShellShortcutCreate(&d,&shortcut);
    if(s<0)goto done;
    if(d.kind==SC_SHELL_SHORTCUT_LEGACY) {
        s=ScBufferCreate(count,&shortcut->legacy);
        if(s>=0)s=ScBufferAppend(shortcut->legacy,p,count);
        if(s<0)goto done;
    }
    *out=shortcut;
    shortcut=0;
    done:for(i=0;i<5;++i)free(strings[i]);
    ScStringRelease(joined);
    ScConfigRelease(config);
    ScShellShortcutRelease(shortcut);
    return sh_cause(s,62,0);
}
ScStatus ScShellShortcutRead(const char *path,ScShellShortcut **out) {
    ScBuffer *b=0;
    const unsigned char *p;
    ScU32 n;
    char *absolute=0,*slash;
    ScStatus s=sh_absolute(path,&absolute);
    if(s>=0)s=sh_read_file(absolute,&b);
    if(s>=0)s=sh_buffer_data(b,&p,&n);
    if(s>=0) {
        slash=strrchr(absolute,'/');
        if(slash)slash[1]=0;
        s=ScShellShortcutParse(p,n,absolute,out);
    }
    free(absolute);
    ScBufferRelease(b);
    return s;
}
ScStatus ScShellShortcutWrite(const ScShellShortcut *s,const char *path,ScU32 expected,ScU32 *revision) {
    ScBuffer *b=0;
    ScStatus status=ScShellShortcutSerialize(s,&b);
    if(status>=0)status=sh_store_file(path,b,expected,revision);
    ScBufferRelease(b);
    return status;
}
static ScStatus sh_legacy_launch(const char *command,ScLaunchInfo *out) {
    char *exe=0,*arguments=0;
    ScStatus s=sh_legacy_fields(command,&exe,&arguments);
    if(s>=0)s=sh_launch_path(exe,arguments,0,SC_SHELL_PROFILE_LEGACY,out);
    free(exe);
    free(arguments);
    return s;
}
ScStatus ScShellShortcutLaunch(const ScShellShortcut *s,const ScShellCatalog *catalog,ScLaunchInfo *out) {
    ScShellApplication *app=0;
    ScU32 verb;
    ScStatus status;
    if(!sh_valid(s,SH_SHORTCUT))return SC_SHELL_E_STALE;
    if(s->desc.kind==SC_SHELL_SHORTCUT_LEGACY)return sh_legacy_launch(s->desc.target,out);
    if(s->desc.kind==SC_SHELL_SHORTCUT_PATH)return ScShellOpen(catalog,s->desc.target,"open",out);
    if(s->desc.kind==SC_SHELL_SHORTCUT_EXECUTABLE)return sh_launch_path(s->desc.target,s->desc.arguments,0,s->desc.profile,out);
    status=ScShellCatalogFind(catalog,s->desc.application,&app);
    if(status<0)return status;
    if(!sh_verb_index(app,"",&verb)) {
        ScShellApplicationRelease(app);
        return SC_SHELL_E_NOT_FOUND;
    }
    /* shortcut参数显式覆盖应用默认，不作为shell代码执行。 */ status=sh_launch_path(*app->verbs[verb].executable?app->verbs[verb].executable:app->desc.executable, *s->desc.arguments?s->desc.arguments:(*app->verbs[verb].arguments?app->verbs[verb].arguments:app->desc.arguments),0,app->verbs[verb].profile,out);
    ScShellApplicationRelease(app);
    return status;
}
static ScStatus sh_icon_memory(ScUiContext *ui,const unsigned char *p,ScU32 n,ScUiIcon **out) {
    ScBinaryReader reader;
    ScU32 w=0,h=0,format=0,reserved=0,bytes=0,trailer=0,i,*pixels=0,palette[256];
    ScStatus s;
    ScThemeInfo theme;
    (void)ui;
    memset(&theme,0,sizeof(theme));
    theme.size=sizeof(theme);
    theme.version=1;
    if(n<24)return SC_SHELL_E_INVALID;
    s=ScBinaryReaderInit(&reader,p,n);
    if(s>=0)s=ScBinarySkip(&reader,8);
    if(s>=0)s=ScBinaryReadU32LE(&reader,&w);
    if(s>=0)s=ScBinaryReadU32LE(&reader,&h);
    if(s>=0)s=ScBinaryReadU32LE(&reader,&format);
    if(s>=0)s=ScBinaryReadU32LE(&reader,&reserved);
    if(s<0)return sh_cause(s,63,0);
    if(!memcmp(p,"SCF1MIO\0",8)&&n==288&&w==1) {
        ScU32 x,y;
        ScU32 label=0;
        while(label<4&&p[12+label])++label;
        if(!label||label==4||ScUtf8Validate(p+12,label,0)<0)return SC_SHELL_E_INVALID;
        for(i=label;i<4;++i)if(p[12+i])return SC_SHELL_E_INVALID;
        s=ScThemeQuery(&theme);
        if(s<0)return sh_cause(s,63,0);
        w=h=16;
        pixels=(ScU32 *)calloc(256,4);
        if(!pixels)return SC_SHELL_E_MEMORY;
        for(y=0;y<16;++y) {
            for(x=0;x<16;++x) {
                unsigned char ch=p[16+y*17+x];
                if(ch!='.'&&ch!='#') {
                    s=SC_SHELL_E_INVALID;
                    goto done;
                }
                if(ch=='#')pixels[y*16+x]=0xFF000000u|theme.colors[SC32_COLOR_ACCENT];
            }
            if(p[16+y*17+16]) {
                s=SC_SHELL_E_INVALID;
                goto done;
            }
        }
    } else {
        if(!w||!h||w>128||h>128)return SC_SHELL_E_RANGE;
        if(!memcmp(p,"SCB2MIO\0",8)&&format==2&&!reserved) {
            s=ScBinaryReadU32LE(&reader,&bytes);
            if(s>=0)s=ScBinaryReadU32LE(&reader,&trailer);
            if(s<0||trailer!=0x004F494Du||bytes!=w*h*4||n!=32+bytes)return SC_SHELL_E_INVALID;
            /* SCB2是已完整核界的i386小端直通ARGB；IconCreate立即复制
             * 成为自己的像素。原实现先另分配一整张图片，每像素跨SCL
             * 调BinaryRead，再复制一次；64px的16个图标就有65536次
             * 无意义逐像素调用。直接交同一原始字节，颜色/alpha/尺寸
             * 全部保留，不借用随后会释放的文件缓冲。索引SCB1和SCF
             * 仍按各自格式转换，不把它们解释成ARGB。 */
            return sh_cause(ScUiIconCreateArgb(w,h,w*4,(const ScU32 *)(p+32),out),63,0);
        } else if(!memcmp(p,"SCB1MIO\0",8)&&format==1&&reserved==0x004F494Du&&n==24+w*h) {
            s=ScDisplayPalette(palette,256);
            if(s<0)return sh_cause(s,63,0);
        } else return SC_SHELL_E_INVALID;
        pixels=(ScU32 *)malloc(w*h*4);
        if(!pixels)return SC_SHELL_E_MEMORY;
        for(i=0;i<w*h;++i) {
            ScU32 slot=p[24+i];
            pixels[i]=slot?0xFF000000u|palette[slot]:0;
        }
    }
    s=ScUiIconCreateArgb(w,h,w*4,pixels,out);
    done:free(pixels);
    return sh_cause(s,63,0);
}
static ScStatus sh_embedded_icon(ScUiContext *ui,const char *executable,ScUiIcon **out) {
    ScFile file=0;
    ScFileOpenDesc open;
    ScFileInfo info,after;
    ScIoResult io;
    unsigned char header[36],*icon=0;
    ScBinaryReader reader;
    ScU32 n=0,entry,load,bss,stack,flags,base,trailer,offset=0;
    ScStatus s;
    memset(&open,0,sizeof(open));
    open.size=sizeof(open);
    open.version=1;
    open.path=executable;
    open.flags=SC32_FILE_READ;
    s=ScFileOpen(&open,&file);
    if(s<0)goto done;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    s=ScFileQuery(file,&info);
    if(s<0)goto done;
    n=info.length;
    if(n<36) {
        s=SC_SHELL_E_INVALID;
        goto done;
    }
    memset(&io,0,sizeof(io));
    io.size=sizeof(io);
    io.version=1;
    s=ScFileRead(file,header,sizeof(header),&io);
    if(s<0)goto done;
    if(io.transferred!=sizeof(header)||memcmp(header,"SCX1MIO\0",8)) {
        s=SC_SHELL_E_INVALID;
        goto done;
    }
    s=ScBinaryReaderInit(&reader,header,sizeof(header));
    if(s>=0)s=ScBinarySkip(&reader,8);
    if(s>=0)s=ScBinaryReadU32LE(&reader,&entry);
    if(s>=0)s=ScBinaryReadU32LE(&reader,&load);
    if(s>=0)s=ScBinaryReadU32LE(&reader,&bss);
    if(s>=0)s=ScBinaryReadU32LE(&reader,&stack);
    if(s>=0)s=ScBinaryReadU32LE(&reader,&flags);
    if(s>=0)s=ScBinaryReadU32LE(&reader,&base);
    if(s>=0)s=ScBinaryReadU32LE(&reader,&trailer);
    if(s<0)goto done;
    if(!load||load>262108||load>n-36||bss>0x3D0000u||load>0x3D0000u-bss||entry>=load||stack<4096||stack>131072|| flags&~3u||base!=0x400000u||trailer!=0x004F494Du) {
        s=SC_SHELL_E_INVALID;
        goto done;
    }
    if(!(flags&2)) {
        s=n==36+load?SC_SHELL_E_NOT_FOUND:SC_SHELL_E_INVALID;
        goto done;
    }
    n-=36+load;
    if(n<32||n>65568) {
        s=SC_SHELL_E_INVALID;
        goto done;
    }
    icon=(unsigned char *)malloc(n);
    if(!icon) {
        s=SC_SHELL_E_MEMORY;
        goto done;
    }
    memset(&after,0,sizeof(after));
    after.size=sizeof(after);
    after.version=1;
    s=ScFileSeek(file,(ScI32)(36+load),SC32_SEEK_SET,&after);
    if(s<0)goto done;
    while(offset<n) {
        ScU32 take=n-offset;
        if(take>65536)take=65536;
        memset(&io,0,sizeof(io));
        io.size=sizeof(io);
        io.version=1;
        s=ScFileRead(file,icon+offset,take,&io);
        if(s<0)goto done;
        if(!io.transferred||io.transferred>take) {
            s=SC_SHELL_E_IO;
            goto done;
        }
        offset+=io.transferred;
    }
    if(memcmp(icon,"SCB2MIO\0",8)) {
        s=SC_SHELL_E_INVALID;
        goto done;
    }
    s=ScFileQuery(file,&after);
    if(s<0)goto done;
    if(after.object_id!=info.object_id||after.revision!=info.revision||after.length!=info.length) {
        s=SC_SHELL_E_STALE;
        goto done;
    }
    s=sh_icon_memory(ui,icon,n,out);
    done:if(file)ScFileClose(file);
    free(icon);
    return s;
}
static ScStatus sh_icon_fallback(ScUiIcon **out) {
    ScStatus s;
    ScThemeInfo theme;
    ScU32 pixels[256];
    ScU32 x,y;
    memset(&theme,0,sizeof(theme));
    theme.size=sizeof(theme);
    theme.version=1;
    s=ScThemeQuery(&theme);
    if(s<0)return sh_cause(s,63,0);
    memset(pixels,0,sizeof(pixels));
    /* 同主题纸页轮廓是坏/缺资源回退，不引入另一组裸RGB。 */ for(y=2;y<14;++y)for(x=4;x<12;++x)pixels[y*16+x]=0xFF000000u|theme.colors[(x==4||x==11||y==2||y==13)?SC32_COLOR_LINE:SC32_COLOR_PAPER];
    for(y=6;y<11;y+=2)for(x=6;x<10;++x)pixels[y*16+x]=0xFF000000u|theme.colors[SC32_COLOR_MUTED];
    return sh_cause(ScUiIconCreateArgb(16,16,64,pixels,out),63,0);
}
static ScResourceCache *sh_icons;
static ScUiContext *sh_icon_ui;
static ScStatus sh_icon_loader(const ScView *key,const ScCacheStamp *stamp,void *context,void **resource,ScU32 *cost) {
    const char *text=(const char *)key->data;
    ScUiIcon *icon=0;
    ScUiIconInfo info;
    ScThemeInfo theme;
    ScStatus s;
    (void)context;
    if(key->bytes<2||text[key->bytes-1]||memchr(text,0,key->bytes-1)||(text[0]!='X'&&text[0]!='F'))return SC_SHELL_E_INVALID;
    s=text[0]=='X'?sh_embedded_icon(sh_icon_ui,text+1,&icon):ScUiIconLoad(sh_icon_ui,text+1,&icon);
    if(s<0)s=sh_icon_fallback(&icon);
    if(s<0)return s;
    memset(&theme,0,sizeof(theme));
    theme.size=sizeof(theme);
    theme.version=1;
    s=ScThemeQuery(&theme);
    if(s>=0&&theme.generation!=stamp->theme_epoch)s=SC_SHELL_E_STALE;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    if(s>=0)s=ScUiIconQuery(icon,&info);
    if(s<0) {
        ScUiIconRelease(icon);
        return s;
    }
    *resource=icon;
    *cost=info.bytes+sizeof(info);
    return 0;
}
static void sh_icon_drop(void *resource,void *context) {
    (void)context;
    if(!sh_quiet)ScUiIconRelease((ScUiIcon *)resource);
}
static ScStatus sh_icon_cache(void) {
    ScCacheDesc d;
    if(sh_icons)return 0;
    memset(&d,0,sizeof(d));
    d.size=sizeof(d);
    d.version=1;
    d.budget_bytes=524288;
    d.load=sh_icon_loader;
    d.drop=sh_icon_drop;
    return sh_cause(ScCacheCreate(&d,&sh_icons),63,0);
}
ScStatus ScShellIconCacheBudget(ScU32 bytes) {
    ScStatus s=sh_icon_cache();
    if(s>=0)s=ScCacheSetBudget(sh_icons,bytes);
    return sh_cause(s,63,0);
}
ScStatus ScShellIconCacheQuery(ScCacheInfo *out) {
    ScStatus s=sh_icon_cache();
    if(s>=0)s=ScCacheQuery(sh_icons,out);
    return sh_cause(s,63,0);
}
ScStatus ScShellIconLoad(ScUiContext *ui,const char *spec,const char *executable,ScUiIcon **out) {
    char path[128],root[65],*absolute=0,*key=0;
    ScU32 n=0,i,lease=0;
    ScUiIcon *icon=0;
    ScCacheStamp stamp;
    ScPathInfo metadata;
    ScThemeInfo theme;
    ScDisplayInfo display;
    ScStatus s;
    if(!ui||!out||sh_icon_ui)return SC_SHELL_E_INVALID;
    s=sh_text(spec,127,1);
    if(s<0)return s;
    if(!spec||!*spec) {
        if(executable&&*executable) {
            char *legacy_exe=0,*legacy_args=0;
            s=sh_legacy_fields(executable,&legacy_exe,&legacy_args);
            if(s>=0)s=sh_absolute(legacy_exe,&absolute);
            free(legacy_exe);
            free(legacy_args);
            if(s<0)return s;
        } else {
            s=sh_copy_text("/",1,0,&absolute);
            if(s<0)return s;
        }
    } else if(*spec=='@') {
        ScU32 length=(ScU32)strlen(spec+1);
        if(!length||length>31)return SC_SHELL_E_INVALID;
        for(i=0;i<length;++i)if(!isalnum((unsigned char)spec[i+1])&&spec[i+1]!='_')return SC_SHELL_E_INVALID;
        s=ScThemePath(SC32_THEME_ICON_ROOT,root,sizeof(root),&n);
        if(s>=0&&n+length+6<=sizeof(path)) {
            snprintf(path,sizeof(path),"%s/%s.SCB",root,spec+1);
            memset(&metadata,0,sizeof(metadata));
            metadata.size=sizeof(metadata);
            metadata.version=1;
            s=ScPathQuery(path,&metadata);
        } else s=SC_SHELL_E_NOT_FOUND;
        if(s<0)snprintf(path,sizeof(path),"/SYS/ICONS/%s.SCB",spec+1);
        s=sh_absolute(path,&absolute);
        if(s<0)return s;
    } else {
        if(spec[0]!='/') {
            if(strlen(spec)+2>sizeof(path))return SC_SHELL_E_RANGE;
            path[0]='/';
            strcpy(path+1,spec);
            spec=path;
        }
        s=sh_absolute(spec,&absolute);
        if(s<0)return s;
    }
    key=(char *)malloc(strlen(absolute)+2);
    if(!key) {
        free(absolute);
        return SC_SHELL_E_MEMORY;
    }
    key[0]=!spec||!*spec?'X':'F';
    strcpy(key+1,absolute);
    /* SandFS只折叠ASCII大小写；cache必须采用同一名字身份，不能让
     * 桌面小写link与开始菜单大写注册名为同一SCX重复读盘/解码。
     * 原显示路径不改，非ASCII字节不做未经FS支持的Unicode折叠。 */
    for(i=1;key[i];i++)if(key[i]>='a' && key[i]<='z')key[i]=(char)(key[i]-'a'+'A');
    memset(&metadata,0,sizeof(metadata));
    metadata.size=sizeof(metadata);
    metadata.version=1;
    s=ScPathQuery(absolute,&metadata);
    if(s<0)metadata.generation=0;
    memset(&theme,0,sizeof(theme));
    theme.size=sizeof(theme);
    theme.version=1;
    s=ScThemeQuery(&theme);
    if(s<0)goto done;
    memset(&display,0,sizeof(display));
    display.size=sizeof(display);
    display.version=1;
    s=ScDisplayQuery(&display);
    if(s<0)goto done;
    memset(&stamp,0,sizeof(stamp));
    stamp.size=sizeof(stamp);
    stamp.version=1;
    stamp.namespace_id=1;
    stamp.object_generation=metadata.generation;
    stamp.theme_epoch=theme.generation;
    stamp.display_epoch=display.generation;
    s=sh_icon_cache();
    if(s<0)goto done;
    sh_icon_ui=ui;
    s=ScCacheAcquire(sh_icons,key,(ScU32)strlen(key)+1,&stamp,(void **)&icon,&lease);
    sh_icon_ui=0;
    if(s>=0) {
        s=ScUiIconRetain(icon);
        ScCacheReleaseLease(sh_icons,lease);
        if(s>=0)*out=icon;
    }
    done:sh_icon_ui=0;
    free(key);
    free(absolute);
    return sh_cause(s,63,0);
}
