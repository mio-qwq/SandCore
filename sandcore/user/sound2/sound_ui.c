static ScStatus sound_value(ScUiNode node,ScU32 value)
{
    ScValue v;memset(&v,0,sizeof(v));v.size=sizeof(v);v.version=1;v.type=SC_VALUE_I32;v.lo=value;
    sound_app.setting_value++;ScStatus s=ScUiNodeSetValue(tool.ui,node,&v);sound_app.setting_value--;return s;
}
static ScU32 sound_position(const SndSlot *s)
{
    if(!s || !s->state.file.rate)return 0;
    ScU32 consumed=s->state.voice.consumed_frames;
    if(consumed>0xffffffffu-s->state.base_frame)consumed=0xffffffffu-s->state.base_frame;
    return (s->state.base_frame+consumed)/s->state.file.rate;
}
static void sound_changed(void)
{
    SndSlot *s=sound_app.current;if(!s || !tool.ui)return;
    const SndMessage *m=&s->state;
    ScUiText title=tool_text(sound_app.track_title[0]?sound_app.track_title:s->path),artist=tool_text(sound_app.track_artist[0]?sound_app.track_artist:"本地音乐");
    ScUiNodeSetText(tool.ui,sound_app.title,&title);ScUiNodeSetText(tool.ui,sound_app.artist,&artist);
    ScU32 position=sound_position(s),total=m->file.rate?m->file.total_frames/m->file.rate:0;
    char details[224];snprintf(details,sizeof(details),"%s   %u Hz   %u 声道   %u bit\n%s",
        m->file.kind==SC_SOUND_MP3?"MP3":m->file.kind==SC_SOUND_FLAC?"FLAC":"WAV",
        m->file.rate,m->file.channels,m->file.bits,s->path);
    ScUiText text=tool_text(details);ScUiNodeSetText(tool.ui,sound_app.details,&text);
    if(position!=sound_app.last_position || total!=sound_app.last_total){
        ScUiProgressSet(tool.ui,sound_app.progress,position,total?total:position+1,SC_UI_PROGRESS_DETERMINATE);
        sound_app.last_position=position;sound_app.last_total=total;
    }
    if(!sound_app.candidate){char message[224];
        snprintf(message,sizeof(message),"%s   %u:%02u / %u:%02u   音量 %u%%   等待队列 %u 帧",
            m->flags&SND_FLAG_FINISHED?"已结束":m->flags&SND_FLAG_PAUSED?"已暂停":"播放中",
            position/60,position%60,total/60,total%60,tool_percent(m->value,256),m->voice.queued_frames);
        tool_status(message);
    }
    if(tool.visible && !sound_app.mini)ScUiInvalidate(tool.ui,sound_app.peaks,0);
}
static ScStatus sound_peak_paint(ScUiContext *ui,ScUiNode node,ScUiPainter *p,void *context)
{
    (void)ui;(void)node;(void)context;ScUiRect b;ScStatus status=ScUiPainterBounds(p,&b);if(status<0)return status;
    ScUiPainterFill(p,&b,SC32_COLOR_PAPER);
    if(b.width<16 || b.height<8)return 0;
    ScUiRect title={b.x+8,b.y+4,b.width-16,20};ScUiText label=tool_text("PCM 峰值 · 最近 64 个解码块");
    ScUiPainterText(p,&title,&label,SC_UI_FONT_DENSE,SC_UI_TEXT_ELLIPSIS);
    ScI32 h=b.height-32;if(h<1)return 0;
    SndSlot *s=sound_app.current;
    for(ScU32 i=0;i<64;i++){
        ScU32 peak=s?s->state.peaks[i]:0;if(peak>32768)peak=32768;
        ScI32 x=b.x+8+(ScI32)i*(b.width-16)/64,w=(b.width-16)/64-1;
        ScI32 height=(ScI32)peak*h/32768;if(w<1)w=1;
        ScUiRect bar={x,b.y+b.height-4-height,w,height};ScUiPainterFill(p,&bar,SC32_COLOR_ACCENT);
    }
    return 0;
}
static void sound_image_cancel(void)
{
    if(sound_app.image){sound_app.image_cancel=1;ScGfxOperationCancel(sound_app.image);sound_app.image_more=1;}
}
static ScStatus sound_cover_step(void)
{
    SndSlot *s=sound_app.current;
    if(!sound_app.image && s && s->cover && s->cover_bytes && s->cover_at==s->cover_bytes){
        ScStatus status=ScGfxLoadMemoryBegin(s->cover,s->cover_bytes,&sound_app.image);
        if(status<0){free(s->cover);s->cover=0;s->cover_bytes=s->cover_at=0;return status;}
        sound_app.image_bytes=s->cover;s->cover=0;s->cover_bytes=s->cover_at=0;
        sound_app.image_cancel=sound_app.image_done=0;sound_app.image_more=1;
    }
    if(!sound_app.image)return 0;
    ScGfxOperationInfo info;memset(&info,0,sizeof(info));info.size=sizeof(info);info.version=1;
    ScStatus status=ScGfxOperationQuery(sound_app.image,&info);if(status<0)return status;
    if(info.state==SC_GFX_OP_RUNNING){
        ScGfxOperationStep(sound_app.image,16384);
        status=ScGfxOperationQuery(sound_app.image,&info);if(status<0)return status;
    }
    sound_app.image_wait=info.wait_events;sound_app.image_more=info.state==SC_GFX_OP_RUNNING && !info.wait_events;
    if(info.state==SC_GFX_OP_RUNNING)return 0;
    ScStatus result=0;
    if(!sound_app.image_done){sound_app.image_done=1;
        if(info.state==SC_GFX_OP_READY && !sound_app.image_cancel){
            ScGfxSurface *surface=0;result=ScGfxOperationSurface(sound_app.image,&surface);
            if(!result)result=ScGfxCanvasSetSurface(sound_app.canvas,surface);
            if(!result){ScGfxSurfaceRelease(sound_app.cover);sound_app.cover=surface;surface=0;}
            ScGfxSurfaceRelease(surface);
        }else if(info.state==SC_GFX_OP_FAILED && !sound_app.image_cancel){
            result=info.cause?info.cause:info.status;
            if(s && !s->fallback && s->state.cover_kind==1)s->fallback=1;
        }
    }
    status=ScGfxOperationClose(sound_app.image);
    if(!status){sound_app.image=0;free(sound_app.image_bytes);sound_app.image_bytes=0;
        sound_app.image_wait=sound_app.image_more=0;}
    else if(status>0)sound_app.image_more=1;
    return result;
}
static ScStatus sound_mode(ScU32 mode)
{
    ScWindowInfo native;memset(&native,0,sizeof(native));native.size=sizeof(native);native.version=1;
    ScStatus status=ScUiWindowNative(tool.ui,tool.root,&native);if(status<0)return status;
    if(mode==S2_FULL){
        if(sound_app.mini){status=sound_mode(S2_MINI);if(status<0)return status;}
        sound_app.full=!sound_app.full;
        return ScUiWindowShow(tool.ui,tool.root,sound_app.full?SC32_WINDOW_MAXIMIZE:SC32_WINDOW_RESTORE);
    }
    if(!sound_app.mini){
        sound_app.restore=native;
        if(native.state&SC32_WINDOW_MAXIMIZED){status=ScUiWindowShow(tool.ui,tool.root,SC32_WINDOW_RESTORE);if(status<0)return status;}
        ScUiMetrics metrics;memset(&metrics,0,sizeof(metrics));metrics.size=sizeof(metrics);metrics.version=1;
        status=ScUiMetricsQuery(tool.ui,tool.root,&metrics);if(status<0)return status;
        ScU32 scale=metrics.scale_percent;
        status=ScWindowPlace(native.window,native.x,native.y,460u*scale/100u,220u*scale/100u,&native);
        if(status<0)return status;
        sound_app.mini=1;sound_app.full=0;
    }else{
        ScWindowInfo *saved=&sound_app.restore;
        status=ScWindowPlace(native.window,saved->x,saved->y,saved->client_width,saved->client_height,&native);
        if(status<0)return status;
        sound_app.mini=0;
        if(saved->state&SC32_WINDOW_MAXIMIZED){status=ScUiWindowShow(tool.ui,tool.root,SC32_WINDOW_MAXIMIZE);sound_app.full=1;}
    }
    tool_visible(sound_app.cover_panel,!sound_app.mini);tool_visible(sound_app.stats_panel,!sound_app.mini);
    tool_visible(tool.menu,!sound_app.mini);tool_visible(sound_app.artist,!sound_app.mini);
    return status;
}
static ScStatus sound_dialog_complete(ScUiContext *ui,const ScUiEvent *event,void *context)
{
    (void)ui;(void)context;if(event->type!=SC_UI_EVENT_DIALOG_COMPLETE)return 0;
    ScStatus status=0;
    if(event->command==SC_UI_DIALOG_OK){ScString *path=0;status=ScUiDialogSelectedPath(sound_app.dialog,0,&path);
        const char *z=0;if(!status)status=ScStringCStr(path,&z);if(!status)status=sound_begin(z,0,0);
        ScStringRelease(path);}
    ScUiDialogRelease(sound_app.dialog);sound_app.dialog=0;
    sound_app.paused=sound_app.dialog_previous_pause;
    if(sound_app.current)sound_app.current->want_pause=sound_app.paused;
    if(sound_app.candidate)sound_app.candidate->want_pause=sound_app.paused;
    if(status<0)tool_error(status,"打开音频；原曲保留");
    return SC_UI_HANDLED;
}
static ScStatus sound_command(ScU32 command)
{
    SndSlot *s=sound_app.current;
    if(command==TOOLS_EXIT){tool.quit=1;return 0;}
    if(command==S2_OPEN){
        if(sound_app.dialog || sound_app.candidate || sound_app.retiring)return SC32_E_BUSY;
        ScUiFileDialogDesc d;memset(&d,0,sizeof(d));d.size=sizeof(d);d.version=1;d.owner=tool.root;
        d.title=tool_text("打开 WAV / MP3 / FLAC");d.flags=SC_UI_FILE_OPEN;d.directory="/HOME";d.completion=sound_dialog_complete;
        sound_app.dialog_previous_pause=sound_app.paused;
        ScStatus status=ScUiFileDialog(tool.ui,&d,&sound_app.dialog);
        if(!status){sound_app.paused=1;if(s)s->want_pause=1;}return status;
    }
    if(command==S2_MINI || command==S2_FULL)return sound_mode(command);
    if(command==TOOLS_ABOUT)return tool_message("SandCore 音乐","WAV / MP3 / FLAC 流式播放\n封面、音量、跳转和迷你模式。解码与设备使用受管的普通用户任务。");
    if(command==S2_COPY)return sound_app.display_path[0]?tool_copy(sound_app.display_path):SC32_E_NOT_FOUND;
    if(command==S2_CANCEL){
        /* QUIT发送成功以后旧worker已可能STOP；这个不可逆交接窗口
         * 不能再取消新候选，否则会把旧曲和新曲一起停止。 */
        if(sound_app.current && sound_app.current->quit_sent)return SC32_E_BUSY;
        sound_slot_stop(sound_app.candidate);tool_status("已取消准备；当前曲目保留");return 0;
    }
    if(command==S2_STOP){sound_slot_stop(sound_app.candidate);sound_slot_stop(s);tool_status("已停止");return 0;}
    if(command==S2_VOLUME){ScValue value;memset(&value,0,sizeof(value));value.size=sizeof(value);value.version=1;
        ScStatus status=ScUiNodeGetValue(tool.ui,sound_app.volume_control,&value);if(status<0)return status;
        if((ScI32)value.lo<0 || value.lo>256)return SC32_E_RANGE;
        sound_app.volume=value.lo;if(s)s->want_volume=value.lo;if(sound_app.candidate)sound_app.candidate->want_volume=value.lo;return 0;}
    if(command==S2_PLAY){
        if(!s){if(!sound_app.display_path[0])return sound_command(S2_OPEN);sound_app.paused=0;return sound_begin(sound_app.display_path,0,0);}
        if(s->state.flags&SND_FLAG_FINISHED){sound_app.paused=0;return sound_begin(s->path,0,s->state.file.file_revision);}
        sound_app.paused=!sound_app.paused;s->want_pause=sound_app.paused;return 0;
    }
    if(!s)return SC32_E_NOT_FOUND;
    ScU32 seconds=sound_position(s);
    if(command==S2_BACK)seconds=seconds<10?0:seconds-10;
    else if(command==S2_FORWARD){if(seconds>0xffffffffu-10u)return SC32_E_RANGE;seconds+=10;}
    else if(command==S2_REPLAY)seconds=0;
    else if(command==S2_SEEK){ScValue value;memset(&value,0,sizeof(value));value.size=sizeof(value);value.version=1;
        ScStatus status=ScUiNodeGetValue(tool.ui,sound_app.seek,&value);if(status<0)return status;
        if((ScI32)value.lo<0)return SC32_E_RANGE;
        seconds=value.lo;}
    else return SC_UI_E_NOT_SUPPORTED;
    return sound_begin(s->path,seconds,s->state.file.file_revision);
}
static ScStatus sound_event(ScUiContext *ui,const ScUiEvent *event,void *context)
{
    (void)ui;(void)context;
    if(event->type==SC_UI_EVENT_VALUE_CHANGED && event->node==sound_app.volume_control && !sound_app.setting_value)
        return sound_command(S2_VOLUME);
    if(event->type==SC_UI_EVENT_KEY_DOWN && event->node==tool.root && event->key==27 && sound_app.full)return sound_mode(S2_FULL);
    if(event->type==SC_UI_EVENT_VISIBILITY && tool.visible)sound_changed();
    return 0;
}
static ScStatus sound_ui(void)
{
    ScStatus status=tool_create("SandCore 音乐",820,560,sound_command,sound_event);if(status<0)return status;
    static const ToolMenuEntry file_menu[]={{"打开…",S2_OPEN,'O',SC32_MOD_CONTROL},{"复制文件路径",S2_COPY,0,0},{"退出",TOOLS_EXIT,0,0}};
    static const ToolMenuEntry play_menu[]={{"播放 / 暂停",S2_PLAY,' ',0},{"停止",S2_STOP,0,0},{"重新播放",S2_REPLAY,'R',SC32_MOD_CONTROL},
        {"后退 10 秒",S2_BACK,0,0},{"前进 10 秒",S2_FORWARD,0,0},{"取消准备",S2_CANCEL,0,0}};
    static const ToolMenuEntry view_menu[]={{"迷你模式",S2_MINI,'M',SC32_MOD_CONTROL},{"最大化 / 还原",S2_FULL,SC32_KEY_F1+10,0},{"关于",TOOLS_ABOUT,0,0}};
    status=tool_menu_part("文件",file_menu,3);if(!status)status=tool_menu_part("播放",play_menu,6);
    if(!status)status=tool_menu_part("视图",view_menu,3);
    if(!status)status=tool_layout(tool.toolbar,SC_UI_FLOW,SC_UI_AUTO,SC_UI_AUTO,0);
    const char *labels[]={"打开","播放 / 暂停","停止","-10秒","+10秒","取消","迷你"};
    ScU32 commands[]={S2_OPEN,S2_PLAY,S2_STOP,S2_BACK,S2_FORWARD,S2_CANCEL,S2_MINI};
    for(ScU32 i=0;!status && i<7;i++){ScUiNode button;status=tool_widget(tool.toolbar,SC_UI_BUTTON,labels[i],commands[i],i==1?112:72,28,0,&button);}
    if(!status)status=tool_widget(tool.content,SC_UI_LABEL,"选择一首音乐",0,SC_UI_AUTO,30,0,&sound_app.title);
    if(!status)status=ScUiNodeSetFont(tool.ui,sound_app.title,SC_UI_FONT_HEADING);
    if(!status)status=tool_widget(tool.content,SC_UI_LABEL,"WAV / MP3 / FLAC",0,SC_UI_AUTO,24,0,&sound_app.artist);
    if(!status)status=tool_widget(tool.content,SC_UI_PROGRESS,"",0,SC_UI_AUTO,14,0,&sound_app.progress);
    ScUiNode main_panel=0;
    if(!status)status=tool_widget(tool.content,SC_UI_PANEL,"",0,SC_UI_AUTO,SC_UI_AUTO,1,&main_panel);
    if(!status)status=tool_layout(main_panel,SC_UI_HBOX,SC_UI_AUTO,SC_UI_AUTO,1);
    if(!status)status=tool_widget(main_panel,SC_UI_PANEL,"",0,SC_UI_AUTO,SC_UI_AUTO,1,&sound_app.cover_panel);
    if(!status)status=tool_layout(sound_app.cover_panel,SC_UI_OVERLAY,SC_UI_AUTO,SC_UI_AUTO,1);
    ScGfxCanvasDesc canvas;memset(&canvas,0,sizeof(canvas));canvas.size=sizeof(canvas);canvas.version=1;
    canvas.ui=tool.ui;canvas.parent=sound_app.cover_panel;canvas.flags=SC_GFX_CANVAS_READONLY;canvas.cache_budget_bytes=1048576;canvas.pixel_quantum=16384;
    if(!status)status=ScGfxCanvasCreate(&canvas,&sound_app.canvas);
    if(!status)status=tool_widget(main_panel,SC_UI_PANEL,"",0,300,SC_UI_AUTO,0,&sound_app.stats_panel);
    if(!status)status=tool_layout(sound_app.stats_panel,SC_UI_VBOX,300,SC_UI_AUTO,0);
    if(!status)status=tool_widget(sound_app.stats_panel,SC_UI_LABEL,"音频格式与文件路径",0,SC_UI_AUTO,70,0,&sound_app.details);
    ScUiClassDesc custom;memset(&custom,0,sizeof(custom));custom.size=sizeof(custom);custom.version=1;custom.name="SandCore.Sound.PcmHistory";custom.paint=sound_peak_paint;
    if(!status)status=ScUiClassRegister(tool.ui,&custom,&sound_app.peak_class);
    if(!status)status=tool_widget(sound_app.stats_panel,sound_app.peak_class,"",0,SC_UI_AUTO,SC_UI_AUTO,1,&sound_app.peaks);
    ScUiNode row=0,label=0,button=0;
    if(!status)status=tool_widget(tool.content,SC_UI_PANEL,"",0,SC_UI_AUTO,SC_UI_AUTO,0,&row);
    if(!status)status=tool_layout(row,SC_UI_FLOW,SC_UI_AUTO,SC_UI_AUTO,0);
    if(!status)status=tool_widget(row,SC_UI_LABEL,"音量",0,40,26,0,&label);
    if(!status)status=tool_widget(row,SC_UI_SPINBOX,"224",S2_VOLUME,100,28,0,&sound_app.volume_control);
    if(!status)status=ScUiSpinRange(tool.ui,sound_app.volume_control,0,256,16);
    if(!status)status=tool_listen(sound_app.volume_control,SC_UI_TARGET_PHASE,sound_event,0);
    if(!status)status=tool_widget(row,SC_UI_LABEL,"跳转至秒",0,76,26,0,&label);
    if(!status)status=tool_widget(row,SC_UI_SPINBOX,"0",S2_SEEK,112,28,0,&sound_app.seek);
    if(!status)status=ScUiSpinRange(tool.ui,sound_app.seek,0,2147483647,10);
    if(!status)status=tool_widget(row,SC_UI_BUTTON,"跳转",S2_SEEK,72,28,0,&button);
    if(!status)status=sound_value(sound_app.volume_control,sound_app.volume);
    if(!status)tool_status("打开音乐即可播放；Ctrl+O 打开，空格暂停，Ctrl+M 迷你模式。");
    return status;
}
