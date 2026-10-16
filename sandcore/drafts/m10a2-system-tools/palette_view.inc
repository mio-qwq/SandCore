static const char *pal_group(ScU32 slot)
{
    if(!slot)return "保留黑";
    if(slot<16)return "PAL_SKY / 天空";
    if(slot<24)return "PAL_SAND_L / 后沙丘";
    if(slot<32)return "PAL_SAND_D / 前沙丘";
    static const char *single[]={"PAL_SUN_CORE","PAL_SUN_EDGE","PAL_STAR","PAL_TITLE","PAL_SHADOW","PAL_CON_BG","PAL_CON_TINT","PAL_CON_WARN","PAL_PANIC","PAL_WIN_TITLE","PAL_TASKBAR"};
    if(slot<=42)return single[slot-32];
    static const char *ramps[]={"PAL_UI_NIGHT","PAL_UI_CYAN","PAL_UI_GOLD","PAL_ROCK","PAL_GRASS","PAL_EARTH","PAL_WATER"};
    if(slot<=98)return ramps[(slot-43)/8];
    static const char *last[]={"PAL_UI_INK","PAL_UI_TEXT","PAL_UI_MUTED","PAL_UI_ALERT","PAL_UI_PANEL","PAL_UI_LINE"};
    return slot<=104?last[slot-99]:"未分配槽";
}
static ScU32 pal_matches(const char *text,const char *needle)
{
    if(!*needle)return 1;
    for(;*text;text++){ScU32 i=0;
        while(needle[i] && text[i]){char a=text[i],b=needle[i];
            if(a>='a' && a<='z')a-=32;
            if(b>='a' && b<='z')b-=32;
            if(a!=b)break;
            i++;
        }
        if(!needle[i])return 1;
    }return 0;
}
static void pal_row(PaletteRow *row,ScU32 index,ScU32 rgb,const char *group,ScU32 registered,ScU32 legacy,ScU32 theme)
{
    memset(row,0,sizeof(*row));
    row->rgb=rgb&0xffffffu;
    row->id=index+1+(theme?256:0);
    row->index=index;
    row->registered=registered;
    row->legacy=legacy;
    snprintf(row->group,sizeof(row->group),"%s",group);
    snprintf(row->hex,sizeof(row->hex),"#%06X",row->rgb);
    snprintf(row->channels,sizeof(row->channels),"%u, %u, %u",row->rgb>>16,(row->rgb>>8)&255,row->rgb&255);
    strcpy(row->state,theme?"主题角色":!index?"保留":registered?"GFX已登记":"未分配");
}
static int pal_compare(const void *a,const void *b)
{
    const PaletteRow *x=pal.rows+*(const ScU32 *)a,*y=pal.rows+*(const ScU32 *)b;
    int s=0;
    if(pal.field==1)s=strcmp(x->group,y->group);
    else if(pal.field==2)s=x->rgb<y->rgb?-1:x->rgb>y->rgb?1:0;
    else if(pal.field==3)s=strcmp(x->state,y->state);
    else s=x->index<y->index?-1:x->index>y->index?1:0;
    if(!s)s=x->id<y->id?-1:x->id>y->id?1:0;
    return pal.descending?-s:s;
}
static ScStatus pal_index(void)
{
    pal.count=0;
    ScU32 base=pal.mode?256:0,total=pal.mode?24:256;
    for(ScU32 i=0;i<280;i++)pal.rank[i]=UINT_MAX;
    for(ScU32 i=base;i<base+total;i++)if(pal_matches(pal.rows[i].group,pal.filter_text) || pal_matches(pal.rows[i].hex,pal.filter_text)
    || pal_matches(pal.rows[i].state,pal.filter_text))pal.order[pal.count++]=i;
    qsort(pal.order,pal.count,4,pal_compare);
    for(ScU32 i=0;i<pal.count;i++)pal.rank[pal.order[i]]=i;
    ScUiDataChange change;
    memset(&change,0,sizeof(change));
    change.size=sizeof(change);
    change.version=1;
    change.kind=SC_UI_DATA_RESET;
    change.revision=++pal.revision;
    return ScUiProviderNotify(pal.provider,&change);
}
static ScUiIcon *pal_icon(ScU32 index)
{
    if(index>=280 || (index<256?!pal.raw_valid:!pal.theme_valid))return 0;
    if(pal.icons[index] && pal.icon_colors[index]==pal.rows[index].rgb)return pal.icons[index];
    /* 这里的颜色是公开DAC/主题资源采样值，alpha掩码不是新UI色号。
    * 图标只按可见行或当前色样创建，窗口隐藏时不生成新的位图。 */
    if(!tool.visible)return 0;
    ScU32 pixel=pal.rows[index].rgb|0xff000000u;
    ScUiIcon *candidate=0;
    if(ScUiIconCreateArgb(1,1,4,&pixel,&candidate)<0)return 0;
    ScUiIconRelease(pal.icons[index]);
    pal.icons[index]=candidate;
    pal.icon_colors[index]=pal.rows[index].rgb;
    return candidate;
}
static ScStatus pal_data(void *context,const ScUiDataRequest *request,ScUiDataReply *reply)
{
    (void)context;
    reply->revision=pal.revision;
    if(request->operation==SC_UI_DATA_COUNT || request->operation==SC_UI_DATA_CHILD_COUNT){reply->total=pal.count;
        return 0;
    }
    if(request->operation==SC_UI_DATA_SORT){pal.field=request->field;
        pal.descending=request->flags&1;
        ScStatus s=pal_index();
        reply->revision=pal.revision;
        return s;
    }
    ScU32 index=request->operation==SC_UI_DATA_CHILD_AT?(request->index<pal.count?pal.order[request->index]:UINT_MAX):request->item-1;
    if(index>=280 || (pal.mode?index<256:index>=256))return SC_UI_END;
    PaletteRow *row=pal.rows+index;
    reply->item=row->id;
    reply->state=SC_UI_VISIBLE|SC_UI_ENABLED;
    if(request->operation==SC_UI_DATA_INDEX_OF){if(pal.rank[index]==UINT_MAX)return SC_UI_E_NOT_FOUND;
        reply->total=pal.rank[index];
    }
    else if(request->operation==SC_UI_DATA_ITEM)reply->icon=pal_icon(index);
    else if(request->operation==SC_UI_DATA_CELL){
        if(!request->field){reply->value.type=SC_VALUE_U32;
            reply->value.lo=row->index;
        }
        else {const char *text=request->field==1?row->group:request->field==2?row->hex:request->field==3?row->state:request->field==4?row->channels:row->legacy?"1–104可达":pal.mode?"独立主题域":"旧版不可达";
            if((request->field==2 || request->field==4) && (pal.mode?!pal.theme_valid:!pal.raw_valid))text="不可用";
            reply->value.type=SC_VALUE_UTF8;
            reply->value.data=text;
            reply->value.bytes=(ScU32)strlen(text);
    }}
    return 0;
}
static ScStatus pal_preview(ScUiContext *ui,ScUiNode node,ScUiPainter *painter,void *context)
{
    (void)ui;
    (void)node;
    (void)context;
    ScUiRect bounds;
    ScStatus s=ScUiPainterBounds(painter,&bounds);
    if(s<0)return s;
    ScUiPainterFill(painter,&bounds,SC32_COLOR_FACE_ALT);
    ScUiRect swatch={12,12,bounds.width-24,bounds.height-24};
    if(swatch.width<=0 || swatch.height<=0)return 0;
    ScUiIcon *icon=pal_icon(pal.selected);
    if(icon)return ScUiPainterIcon(painter,&swatch,icon,SC_UI_ENABLED);
    ScUiText unavailable=tool_text("色样不可用");
    return ScUiPainterText(painter,&swatch,&unavailable,SC_UI_FONT_BODY,SC_UI_TEXT_CENTER);
}
static void pal_details(void)
{
    PaletteRow *row=pal.rows+pal.selected;
    char detail[224];
    snprintf(detail,sizeof(detail),"%s %u · %s\n%s",pal.mode?"主题角色":"原始DAC槽",row->index,row->state,row->group);
    ScUiText text=tool_text(detail);
    ScUiNodeSetText(tool.ui,pal.details,&text);
    ScU32 valid=pal.mode?pal.theme_valid:pal.raw_valid,stale=pal.mode?pal.theme_stale:pal.raw_stale;
    text=tool_text(valid?row->hex:"不可用");
    ScUiNodeSetText(tool.ui,pal.hex,&text);
    text=tool_text(valid?row->channels:"不可用");
    ScUiNodeSetText(tool.ui,pal.rgb,&text);
    char source[192];snprintf(source,sizeof(source),"%s %u · %s · RGB %s%s",pal.mode?"主题角色":"DAC槽",row->index,
    valid?row->hex:"不可用",valid?row->channels:"不可用",stale?"（旧快照）":"");
    text=tool_text(source);
    ScUiNodeSetText(tool.ui,pal.source,&text);
    pal.sync=1;
    ScValue value;
    memset(&value,0,sizeof(value));
    value.size=sizeof(value);
    value.version=1;
    value.type=SC_VALUE_I32;
    value.lo=row->index;
    ScUiSpinRange(tool.ui,pal.slot,0,pal.mode?23:255,1);
    ScUiNodeSetValue(tool.ui,pal.slot,&value);
    ScUiSelectionSet(pal.selection,row->id,SC_UI_REPLACE);
    ScUiViewEnsureItem(tool.ui,pal.table,row->id);
    pal.sync=0;
    if(tool.visible)ScUiInvalidate(tool.ui,pal.preview,0);
}
static ScStatus pal_refresh(void)
{
    ScU32 raw[256];
    ScStatus first=ScDisplayPalette(raw,256);
    pal.raw_stale=first<0;
    if(first==0){memcpy(pal.raw,raw,sizeof(raw));
        pal.raw_valid=1;
    }
    ScThemeInfo theme;
    memset(&theme,0,sizeof(theme));
    theme.size=sizeof(theme);
    theme.version=1;
    ScStatus s=ScThemeQuery(&theme);
    pal.theme_stale=s<0;
    if(s==0){pal.theme=theme;
        pal.theme_valid=1;
    }else if(first==0)first=s;
    for(ScU32 i=0;i<256;i++)pal_row(pal.rows+i,i,pal.raw[i],pal_group(i),i<=104,i>=1 && i<=104,0);
    static const char *roles[]={"FACE","FACE_ALT","PAPER","TEXT","MUTED","LINE","ACCENT","ACCENT_DEEP","SELECT","SELECT_TEXT","SHADOW","ALERT","GOLD","GREEN","VIOLET","ROSE","HIGHLIGHT","DARK_EDGE","TITLE_ACTIVE","TITLE_INACTIVE","TITLE_TEXT","TITLE_MUTED","DESKTOP_TEXT","DESKTOP_SHADOW"};
    for(ScU32 i=0;i<24;i++)pal_row(pal.rows+256+i,i,pal.theme.colors[i],roles[i],1,0,1);
    pal_index();
    pal_details();
    tool_status(first<0?"刷新失败；保留此前实际快照，未替换为零":"已读取实际DAC与主题快照；未分配不等于可任意改写");
    return first;
}
static ScStatus pal_wallpaper(ScU32 mode)
{
    ScStatus s=ScWallpaperSet(mode);
    if(s<0)return s;
    ScWallpaperInfo info;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    s=ScWallpaperQuery(&info);
    tool_status(s<0?"程序壁纸模式已保存；当前视图状态不可读取":info.flags&SC32_WALL_IMAGE_SELECTED?
    "模式已保存；主题选择了图片，图片成功加载时优先显示":"程序壁纸模式已持久保存到本人账户配置");
    return 0;
}
static ScStatus pal_command(ScU32 command)
{
    if(command==TOOLS_EXIT){tool.quit=1;
        return ScUiContextStop(tool.ui,0);
    }
    if(command==TOOLS_REFRESH)return pal_refresh();
    if(command==PAL_RAW || command==PAL_THEME){pal.mode=command==PAL_THEME;
        pal.selected=pal.mode?256:1;
        pal_index();
        pal_details();
        return 0;
    }
    if(command==TOOLS_COPY || command==PAL_COPY_RGB){if(pal.selected<256?!pal.raw_valid:!pal.theme_valid)return SC32_E_AGAIN;
        return tool_copy(command==TOOLS_COPY?pal.rows[pal.selected].hex:pal.rows[pal.selected].channels);
    }
    if(command>=PAL_DUNES && command<=PAL_SAND)return pal_wallpaper(command-PAL_DUNES);
    if(command==TOOLS_ABOUT)return tool_message("Palette 2","DAC完整256槽；GFX已登记0–104，旧Palette只可达1–104。\n105–255未分配。主题24角色另列，不能拿主题纸色冒充原DAC。\n所有色样和复制值均来自真实公开快照。");
    return SC_UI_E_NOT_SUPPORTED;
}
static ScStatus pal_event(ScUiContext *ui,const ScUiEvent *event,void *context)
{
    (void)ui;
    (void)context;
    if(pal.sync)return 0;
    if(event->type==SC_UI_EVENT_KEY_DOWN && event->node==pal.table && !event->modifiers && event->key>='1' && event->key<='3'){
        ScStatus status=pal_wallpaper(event->key-'1');if(status<0)tool_error(status,"保存程序壁纸");return SC_UI_HANDLED;}
    if(event->type==SC_UI_EVENT_SELECTION_CHANGED && event->node==pal.table){ScUiSelectionInfo info;
        memset(&info,0,sizeof(info));
        info.size=sizeof(info);
        info.version=1;
        if(ScUiSelectionQuery(pal.selection,&info)==0 && info.current && info.current<=280){pal.selected=info.current-1;
            pal_details();
    }}
    if(event->type==SC_UI_EVENT_VALUE_CHANGED && event->node==pal.slot){ScValue value;
        memset(&value,0,sizeof(value));
        value.size=sizeof(value);
        value.version=1;
        if(ScUiNodeGetValue(tool.ui,pal.slot,&value)==0 && value.lo<(pal.mode?24u:256u)){pal.selected=value.lo+(pal.mode?256:0);
            pal_details();
    }}
    if(event->type==SC_UI_EVENT_VALUE_CHANGED && event->node==pal.filter){ScString *value=0;
        const char *z=0;
        ScStatus s=tool_string_node(pal.filter,&value,&z);
        if(s==0 && strlen(z)>=sizeof(pal.filter_text))s=SC_UI_E_RANGE;
        if(s==0){strcpy(pal.filter_text,z);
            pal_index();
        }
        ScStringRelease(value);
        if(s<0)tool_error(s,"过滤文本过长");
    }
    if(event->type==SC_UI_EVENT_VISIBILITY && tool.visible)ScUiInvalidate(tool.ui,pal.preview,0);
    if(event->type==SC_UI_EVENT_LAYOUT && event->node==tool.root && pal.details_panel){
        ScUiNodeInfo info;memset(&info,0,sizeof(info));info.size=sizeof(info);info.version=1;
        if(tool.metrics.scale_percent && ScUiNodeQuery(tool.ui,tool.root,&info)==0){
            ScU32 show=(ScU32)info.rectangle.width*100/tool.metrics.scale_percent>=760 && (ScU32)info.rectangle.height*100/tool.metrics.scale_percent>=480;
            tool_visible(pal.details_panel,show);tool_visible(pal.splitter,show);}
        if(tool.metrics.theme_epoch!=pal.observed_theme_epoch){pal.observed_theme_epoch=tool.metrics.theme_epoch;
            /* 切主题只重取主题值，不把它写入DAC；以实际代数避免每次布局重复读。 */
            pal_refresh();}
    }
    return 0;
}
static ScStatus pal_create(void)
{
    memset(&pal,0,sizeof(pal));
    pal.revision=1;
    pal.selected=1;
    ScStatus s=tool_create("Palette 2",1000,660,pal_command,pal_event);
    if(s<0)return s;
    ScUiNode filter_row,body,label;
    s=tool_widget(tool.content,SC_UI_PANEL,"",0,SC_UI_AUTO,32,0,&filter_row);
    if(s==0)s=tool_widget(filter_row,SC_UI_LABEL,"筛选用途 / HEX",0,130,SC_UI_AUTO,0,&label);
    if(s==0)s=tool_widget(filter_row,SC_UI_TEXTBOX,"",0,SC_UI_AUTO,SC_UI_AUTO,1,&pal.filter);
    if(s==0)s=tool_widget(filter_row,SC_UI_SPINBOX,"",0,100,SC_UI_AUTO,0,&pal.slot);
    if(s==0)s=tool_widget(tool.content,SC_UI_LABEL,"",0,SC_UI_AUTO,26,0,&pal.source);
    if(s==0)s=tool_widget(tool.content,SC_UI_PANEL,"",0,SC_UI_AUTO,SC_UI_AUTO,1,&body);
    if(s==0)s=tool_widget(body,SC_UI_TABLE,"",0,SC_UI_AUTO,SC_UI_AUTO,1,&pal.table);
    if(s==0)s=tool_widget(body,SC_UI_SPLITTER,"",0,6,SC_UI_AUTO,0,&pal.splitter);
    if(s==0)s=tool_widget(body,SC_UI_PANEL,"",0,260,SC_UI_AUTO,0,&pal.details_panel);
    if(s==0)s=tool_layout(pal.details_panel,SC_UI_VBOX,260,SC_UI_AUTO,0);
    ScUiClassDesc custom;
    memset(&custom,0,sizeof(custom));
    custom.size=sizeof(custom);
    custom.version=1;
    custom.name="SandCore.Palette.Sample";
    custom.paint=pal_preview;
    if(s==0)s=ScUiClassRegister(tool.ui,&custom,&pal.preview_class);
    if(s==0)s=tool_widget(pal.details_panel,pal.preview_class,"",0,SC_UI_AUTO,SC_UI_AUTO,1,&pal.preview);
    if(s==0)s=tool_widget(pal.details_panel,SC_UI_LABEL,"",0,SC_UI_AUTO,66,0,&pal.details);
    if(s==0)s=tool_widget(pal.details_panel,SC_UI_LABEL,"",0,SC_UI_AUTO,30,0,&pal.hex);
    if(s==0)s=tool_widget(pal.details_panel,SC_UI_LABEL,"",0,SC_UI_AUTO,28,0,&pal.rgb);
    ScUiNode button;
    if(s==0)s=tool_widget(pal.details_panel,SC_UI_BUTTON,"复制HEX",TOOLS_COPY,SC_UI_AUTO,32,0,&button);
    if(s==0)s=tool_widget(pal.details_panel,SC_UI_BUTTON,"复制RGB",PAL_COPY_RGB,SC_UI_AUTO,32,0,&button);
    ScUiProviderDesc provider;
    memset(&provider,0,sizeof(provider));
    provider.size=sizeof(provider);
    provider.version=1;
    provider.flags=SC_UI_PROVIDER_SORT;
    provider.procedure=pal_data;
    if(s==0)s=ScUiProviderCreate(&provider,&pal.provider);
    if(s==0)s=ScUiSelectionCreate(pal.provider,SC_UI_SELECTION_ALLOW_EMPTY,&pal.selection);
    if(s==0)s=ScUiViewBind(tool.ui,pal.table,pal.provider,pal.selection);
    ScUiColumnDesc columns[6];
    memset(columns,0,sizeof(columns));
    const char *titles[]={"编号","GFX用途 / 主题角色","HEX","登记状态","RGB","旧版边界"};
    for(ScU32 i=0;i<6;i++){columns[i].size=sizeof(columns[i]);
        columns[i].version=1;
        columns[i].column=i+1;
        columns[i].field=i;
        columns[i].flags=SC_UI_COLUMN_SORTABLE;
        columns[i].title=tool_text(titles[i]);
        columns[i].width=i==1?230:i==0?70:130;
        columns[i].min_width=44;
        columns[i].max_width=1200;
        columns[i].format=i?SC_UI_FORMAT_TEXT:SC_UI_FORMAT_U32;
    }
    if(s==0)s=ScUiTableSetColumns(tool.ui,pal.table,columns,6);
    ToolMenuEntry view[]={{"原始DAC 256槽",PAL_RAW,0,0},{"主题24角色",PAL_THEME,0,0},{"刷新",TOOLS_REFRESH,SC32_KEY_F1+4,0},{"关闭",TOOLS_EXIT,'w',SC32_MOD_CONTROL}};
    ToolMenuEntry copy[]={{"复制HEX",TOOLS_COPY,'c',SC32_MOD_CONTROL},{"复制RGB",PAL_COPY_RGB,0,0}};
    ToolMenuEntry wall[]={{"沙丘",PAL_DUNES,'1',SC32_MOD_ALT},{"夜色",PAL_NIGHT,'2',SC32_MOD_ALT},{"暖沙",PAL_SAND,'3',SC32_MOD_ALT}};
    ToolMenuEntry help[]={{"颜色来源与边界",TOOLS_ABOUT,SC32_KEY_F1,0}};
    if(s==0)s=tool_menu_part("查看",view,4);
    if(s==0)s=tool_menu_part("复制",copy,2);
    if(s==0)s=tool_menu_part("程序壁纸",wall,3);
    if(s==0)s=tool_menu_part("帮助",help,1);
    ScUiMenu *toolbar=0;
    if(s==0)s=ScUiMenuCreate(&toolbar);
    for(ScU32 i=0;s==0 && i<3;i++)s=tool_menu_item(toolbar,view+i,0);
    if(s==0)s=ScUiToolbarBind(tool.ui,tool.toolbar,toolbar);
    ScUiMenuRelease(toolbar);
    if(s==0)s=tool_listen(pal.table,SC_UI_TARGET_PHASE,pal_event,0);
    if(s==0)s=tool_listen(pal.slot,SC_UI_TARGET_PHASE,pal_event,0);
    if(s==0)s=tool_listen(pal.filter,SC_UI_TARGET_PHASE,pal_event,0);
    if(s<0)return s;
    s=pal_refresh();
    if(s<0)tool_error(s,"读取调色板");
    ScUiFocus(tool.ui,pal.table);
    return 0;
}
