#include "IMAGEAPP.H"

static void image_abort_intent(ImageApp *a)
{a->intent=a->after_save=a->closing=0;
    image_drop_candidate(a);
}
static void image_dialog_poll(ImageApp *a)
{
    if(!a->dialog_done || !a->dialog)return;
    ScUiDialog *dialog=a->dialog;
    ScU32 kind=a->dialog_kind,command=a->dialog_command;
    a->dialog=0;
    a->dialog_kind=a->dialog_done=a->dialog_command=0;
    ScString *selected=0;
    char path[65]={0};
    ScStatus status=0;
    if((kind==I_DIALOG_OPEN || kind==I_DIALOG_SAVE || kind==I_DIALOG_EXPORT) && command==SC_UI_DIALOG_OK){
        status=ScUiDialogSelectedPath(dialog,0,&selected);
        const char *text=0;
        if(status==0)status=ScStringCStr(selected,&text);
        if(status==0){ScU32 length=(ScU32)strlen(text);
            if(length>=sizeof(path))status=SC_UI_E_RANGE;
            else memcpy(path,text,length+1);
    }}
    ScStringRelease(selected);
    ScUiDialogRelease(dialog);
    if(kind==I_DIALOG_DISCARD){
        if(command==I_DISCARD){if(a->intent==I_INTENT_REPLACE)image_commit_candidate(a);
            else if(a->intent==I_INTENT_CLOSE){a->closed=1;
                ScUiContextStop(a->ui,0);
        }}
        else if(command==I_SAVE_CHANGES){a->after_save=a->intent;
            status=a->native_path && a->path[0]?image_save(a,a->path,SC_GFX_FILE_SCB2,a->file_revision):image_file_dialog(a,I_DIALOG_SAVE);
            if(status<0){image_abort_intent(a);
                image_error(a,"Save changes",status);
        }}
        else image_abort_intent(a);
        return;
    }
    if(kind!=I_DIALOG_OPEN && kind!=I_DIALOG_SAVE && kind!=I_DIALOG_EXPORT)return;
    if(command!=SC_UI_DIALOG_OK){if(a->after_save)image_abort_intent(a);
        return;
    }
    if(status<0){if(a->after_save)image_abort_intent(a);
        image_error(a,"Selected path",status);
        return;
    }
    if(kind==I_DIALOG_OPEN)status=image_start_load(a,path);
    else{ScPathInfo info;
        image_record(&info,sizeof(info));
        status=ScPathQuery(path,&info);
        ScU32 revision=0;
        if(status==SC32_E_NOT_FOUND)status=0;
        else if(status==0){if(info.kind!=SC32_PATH_FILE)status=SC32_E_INVALID;
            else revision=info.generation;
        }
        if(status==0)status=image_save(a,path,kind==I_DIALOG_SAVE?SC_GFX_FILE_SCB2:SC_GFX_FILE_BMP,revision);
    }
    if(status<0){if(a->after_save)image_abort_intent(a);
        image_error(a,"File operation",status);
    }
}
static void image_operation_poll(ImageApp *a)
{
    if(!a->operation)return;
    if(!a->operation_complete){ScGfxOperationStep(a->operation,16384);
        ScGfxOperationInfo info;
        image_record(&info,sizeof(info));
        ScGfxOperationQuery(a->operation,&info);
        if(info.state==SC_GFX_OP_RUNNING){char text[96];
            if(info.flags&SC_GFX_OPERATION_ROLLBACK)snprintf(text,sizeof(text),"Restoring previous pixels... %u / %u",info.progress,info.total);
            else if(info.wait_events)snprintf(text,sizeof(text),"Waiting for image service...");
            else snprintf(text,sizeof(text),"Working... %u / %u",info.progress,info.total);
            if(strcmp(text,a->message))image_status(a,text);
            return;
        }
        a->completed=info;
        a->operation_complete=1;
        if(info.state==SC_GFX_OP_READY && info.kind==SC_GFX_OPERATION_LOAD && !a->closing){ScStatus s=ScGfxOperationSurface(a->operation,&a->candidate);
            if(s<0){a->completed.state=SC_GFX_OP_FAILED;
                a->completed.status=s;
            }else{a->candidate_native=info.format==SC_GFX_FILE_SCB2;
                a->candidate_revision=info.file_revision;
                snprintf(a->candidate_path,sizeof(a->candidate_path),"%s",info.path);
                a->source_format=info.format;
        }}
    }
    ScStatus close=ScGfxOperationClose(a->operation);
    if(close==SC_GFX_PENDING)return;
    if(close<0){if(a->failure!=close){a->failure=close;
            image_status(a,"Resource release failed; operation retained for retry");
        }
        a->release_wait=1;
        /* 后端IO失败不丢活lease，也不以Yield空转。用户仍可明确丢弃
         * dirty作品后退出，由正式owner停止链继续回收本人资源。 */
        if(a->closing && !a->dialog){if(image_dirty(a))image_discard_dialog(a);
            else{a->closed=1;
                ScUiContextStop(a->ui,0);
        }}
        return;
    }
    a->release_wait=0;
    a->operation=0;
    a->operation_complete=0;
    ScGfxOperationInfo info=a->completed;
    if(info.state==SC_GFX_OP_READY){
        if(info.kind==SC_GFX_OPERATION_LOAD && !a->closing){a->intent=I_INTENT_REPLACE;
            image_consider_intent(a);
        }
        else if(info.kind==SC_GFX_OPERATION_SAVE){if(info.format==SC_GFX_FILE_SCB2){snprintf(a->path,sizeof(a->path),"%s",info.path);
                a->file_revision=info.file_revision;
                a->native_path=1;
                image_refresh(a);
                image_status(a,"Native image saved");
                if(a->after_save){a->intent=a->after_save;
                    a->after_save=0;
                    image_consider_intent(a);
            }}
            else image_status(a,"BMP exported; native document state retained");
        }
        else{ScGfxCanvasInvalidate(a->view,0);
            image_refresh(a);
            image_status(a,"Operation complete");
        }
        if(info.kind==SC_GFX_OPERATION_COPY && a->cut_pending && !a->closing){a->cut_pending=0;
            ScStatus s=ScGfxClearRectBegin(a->surface,&a->cut_rect,0,&a->operation);
            if(s<0)image_error(a,"Cut pixels",s);
            else a->operation_kind=I_CUT;
        }
    }else{a->cut_pending=0;
        ScGfxCanvasInvalidate(a->view,0);
        image_refresh(a);
        if(a->after_save)image_abort_intent(a);
        if(info.state==SC_GFX_OP_CANCELLED)image_status(a,"Cancelled; previous pixels retained");
        else image_error(a,"Image operation",info.cause?info.cause:info.status);
    }
    image_consider_intent(a);
}
static void image_wait(ImageApp *a)
{
    ScU32 mask=SC32_EVENT_INPUT|SC32_EVENT_STREAM|SC32_EVENT_VISIBILITY;
    if(a->operation && !a->release_wait){ScGfxOperationInfo info;
        image_record(&info,sizeof(info));
        ScGfxOperationQuery(a->operation,&info);
        if(!info.wait_events || info.state!=SC_GFX_OP_RUNNING){ScProcessYield();
            return;
        }mask|=info.wait_events;
    }
    /* ContextStep始终只在主循环非阻塞泵一次；命令/完成回调先入队再在
     * 此处等待。不能在Step(INFINITE)内部消费命令后又睡到下一次输入。 */
    ScU32 timeout=SC32_INFINITE;
    ScUiContextWaitTimeout(a->ui,SC32_INFINITE,&timeout);
    if(a->release_wait && timeout>25u)timeout=25u;
    ScBrokerWaitDesc d;
    image_record(&d,sizeof(d));
    d.event_mask=mask;
    d.timeout=timeout;
    ScU32 events=0;
    ScBrokerWait(&d,&events);
}
static void image_dispose(ImageApp *a)
{
    /* 正常关闭先取消并收拢真实异步对象；构造失败也走同一路径。异常退出
     * 的内核owner清理仍独立存在，不把仅隐藏窗口当资源释放。 */
    if(a->operation){ScGfxOperationCancel(a->operation);
        for(;;){ScGfxOperationInfo i;
            image_record(&i,sizeof(i));
            ScGfxOperationQuery(a->operation,&i);
            if(i.state==SC_GFX_OP_RUNNING)ScGfxOperationStep(a->operation,16384);
            ScStatus s=ScGfxOperationClose(a->operation);
            if(s==0){a->operation=0;
                break;
            }if(s<0)break;
            ScProcessYield();
    }}
    ScUiDialogRelease(a->dialog);
    a->dialog=0;
    ScGfxCanvasRelease(a->view);
    a->view=0;
    ScGfxSurfaceRelease(a->surface);
    ScGfxSurfaceRelease(a->candidate);
    ScUiContextRelease(a->ui);
    ScUiSelectionRelease(a->history_selection);
    ScUiProviderRelease(a->history_provider);
    ScUiMenu *menus[]={a->menu,a->file_menu,a->edit_menu,a->view_menu,a->image_menu,a->help_menu,a->tool_menu,a->drawing_menu};
    for(ScU32 i=0;i<8;i++)ScUiMenuRelease(menus[i]);
    while(a->commands){ImageCommand *c=a->commands;
        a->commands=c->next;
        free(c);
    }
}
static int image_main(void)
{
    ImageApp *a=calloc(1,sizeof(*a));
    if(!a)return 1;
    ScStatus status=ScDisplayPalette(a->palette,256);
    static const ScU32 slots[]={100,99,58,66,102,11,22,34};
    if(status==0)for(ScU32 i=0;i<8;i++)a->colors[i]=0xff000000u|a->palette[slots[i]];
    image_record(&a->brush,sizeof(a->brush));
    a->brush.argb=a->colors[1];
    a->brush.radius=3;
    a->brush.opacity=255;
    a->brush.blend=SC_GFX_BLEND_OVER;
    if(status==0 && IMAGE_APP_CANVAS){ScGfxSurfaceDesc d;
        image_record(&d,sizeof(d));
        d.width=512;
        d.height=320;
        d.fill_argb=a->colors[0];
        status=ScGfxSurfaceCreate(&d,&a->surface);
    }
    if(status==0)status=image_build(a);
    if(status==0){char arguments[1024];
        ScU32 length=0;
        status=ScProcessArguments(arguments,sizeof(arguments),&length);
        if(status==0 && length){const char *start=arguments;
            while(*start==' ')start++;
            if(!strncmp(start,"--open ",7))start+=7;
            char path[65];
            ScU32 n=(ScU32)strlen(start);
            if(n>1 && start[0]=='"' && start[n-1]=='"'){start++;
                n-=2;
            }if(!n || n>=sizeof(path))status=SC_UI_E_RANGE;
            else{memcpy(path,start,n);
                path[n]=0;
                status=image_start_load(a,path);
        }}
        else if(status==0 && !IMAGE_APP_CANVAS)status=image_start_load(a,"/SYS/PICTURES/WELCOME.PNG");
        if(status<0){image_error(a,"Open image",status);
            status=0;
        }
        while(!a->closed && status>=0){status=ScUiContextStep(a->ui,0);
            if(status==SC_UI_END)break;
            image_dialog_poll(a);
            while(a->commands){ImageCommand *c=a->commands;
                a->commands=c->next;
                if(!a->commands)a->last_command=0;
                ScU32 command=c->command;
                free(c);
                ScStatus s=image_execute(a,command);
                if(s<0)image_error(a,"Command",s);
                if(a->closed)break;
            }
            image_operation_poll(a);
            image_consider_intent(a);
            if(!a->closed && !a->commands && !a->dialog_done)image_wait(a);
    }}
    image_dispose(a);
    free(a);
    return status<0?1:0;
}
