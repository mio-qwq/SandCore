static TerminalSession *a_session_id(ScU32 id)
{
    for(TerminalSession *s=terminal_app.sessions;s;s=s->next)if(s->id==id)return s;
    return 0;
}
static ScUiNode a_selected(void)
{
    ScValue value;
    a_record(&value,sizeof(value));
    return ScUiNodeGetValue(terminal_app.ui,terminal_app.tabs,&value)<0?0:value.lo;
}
static ScStatus a_control(TerminalSession *session,ScU32 action)
{
    if(!session || !session->master)return SC32_E_STALE;
    ScStatus status=ScTerminalControl(session->master,action);
    if(!status && action==SC32_TERMINAL_INTERRUPT){
        ScU32 taken=0;
        do {
            taken=0;
            ScUiTerminalTakeInput(session->terminal,session->pending,sizeof(session->pending),&taken);
        }
        while(taken);
        session->pending_at=session->pending_bytes=0;
    }
    return status;
}
static int a_session_remove(TerminalSession *s)
{
    if(s->job || s->master)return 0;
    TerminalSession **link=&terminal_app.sessions;
    while(*link && *link!=s)link=&(*link)->next;
    if(!*link)return 0;
    /* 先撤页，公共Tab自行选真实相邻页；不保存被销毁控件的裸指针。 */
    ScStatus status=ScUiTabRemove(terminal_app.ui,terminal_app.tabs,s->page);
    if(status<0){
        a_error(status,"关闭终端页失败");
        return 0;
    }
    *link=s->next;
    terminal_app.session_count--;
    if(terminal_app.cursor==s)terminal_app.cursor=s->next?s->next:terminal_app.sessions;
    if(terminal_app.active==s)terminal_app.active=0;
    ScUiTerminalRelease(s->terminal);
    free(s);
    ScUiNode selected=a_selected();
    for(TerminalSession *current=terminal_app.sessions;current;current=current->next)if(current->page==selected)terminal_app.active=current;
    if(!terminal_app.sessions && !terminal_app.quitting)a_status("会话已结束。选择“新建会话”继续。");
    return 1;
}
static void a_session_close(TerminalSession *s)
{
    if(!s)return;
    s->closing=1;
    if(s->job && !s->cancel_sent){
        ScStatus status=ScJobCancel(s->job);
        if(status<0 && status!=SC32_E_STALE){
            a_error(status,"无法取消终端作业");
            return;
        }
        s->cancel_sent=1;
    }
    if(s->master){
        ScStatus status=ScTerminalControl(s->master,SC32_TERMINAL_HANGUP);
        if(status<0 && status!=SC32_E_STALE && status!=SC32_E_BROKEN_PIPE)a_error(status,"无法挂断终端");
    }
}
static ScStatus a_session_new(void)
{
    if(terminal_app.quitting || terminal_app.next_id==UINT_MAX)return SC_UI_E_BUSY;
    TerminalSession *s=(TerminalSession *)calloc(1,sizeof(*s));
    if(!s)return SC_UI_E_MEMORY;
    s->id=++terminal_app.next_id;
    ScUiWidgetDesc page;
    a_record(&page,sizeof(page));
    page.parent=terminal_app.tabs;
    page.kind=SC_UI_PANEL;
    page.text=a_text("");
    ScStatus status=ScUiWidgetCreate(terminal_app.ui,&page,&s->page);
    ScUiLayoutDesc layout;
    a_record(&layout,sizeof(layout));
    layout.mode=SC_UI_DOCK_LAYOUT;
    layout.width=layout.height=SC_UI_AUTO;
    layout.max_width=layout.max_height=SC_UI_AUTO;
    layout.column_span=layout.row_span=1;
    if(!status)status=ScUiNodeSetLayout(terminal_app.ui,s->page,&layout);
    ScUiTerminalDesc desc;
    a_record(&desc,sizeof(desc));
    desc.parent=s->page;
    desc.columns=80;
    desc.rows=24;
    desc.history_rows=2000;
    desc.input_budget_bytes=65536;
    desc.font_role=SC_UI_FONT_CODE;
    desc.flags=SC_UI_TERM_CURSOR|SC_UI_TERM_WRAP;
    if(!status)status=ScUiTerminalCreate(terminal_app.ui,&desc,&s->terminal,&s->node);
    layout.mode=SC_UI_ABSOLUTE;
    layout.dock=SC_UI_DOCK_FILL;
    layout.flex_weight=1;
    if(!status)status=ScUiNodeSetLayout(terminal_app.ui,s->node,&layout);
    snprintf(s->label,sizeof(s->label),"Shell %u",s->id);
    ScUiText title=a_text(s->label);
    if(!status)status=ScUiTabAdd(terminal_app.ui,terminal_app.tabs,s->page,&title);
    if(!status)status=ScUiTabSelect(terminal_app.ui,terminal_app.tabs,s->page);
    if(!status)status=ScUiLayout(terminal_app.ui,terminal_app.root);
    ScUiTerminalInfo dimensions;
    a_record(&dimensions,sizeof(dimensions));
    if(!status)status=ScUiTerminalQuery(s->terminal,&dimensions);
    ScTerminalDesc transport;
    a_record(&transport,sizeof(transport));
    transport.columns=dimensions.columns;
    transport.rows=dimensions.rows;
    ScInteractiveInfo spawned;
    a_record(&spawned,sizeof(spawned));
    if(!status)status=ScProcessSpawnInteractive(terminal_app.command,&transport,&spawned);
    if(status<0){
        if(s->page)ScUiNodeDestroy(terminal_app.ui,s->page);
        ScUiTerminalRelease(s->terminal);
        free(s);
        return status;
    }
    s->job=spawned.job;
    s->master=spawned.master;
    s->columns=transport.columns;
    s->rows=transport.rows;
    s->next=terminal_app.sessions;
    terminal_app.sessions=s;
    terminal_app.session_count++;
    terminal_app.active=s;
    if(!terminal_app.cursor)terminal_app.cursor=s;
    ScUiTabSelect(terminal_app.ui,terminal_app.tabs,s->page);
    ScUiFocus(terminal_app.ui,s->node);
    a_status("Shell 已连接");
    return 0;
}
static int a_session_step(TerminalSession *s,ScU32 *budget)
{
    int more=0,input_blocked=0;
    if(s->job){
        ScJobInfo info;
        a_record(&info,sizeof(info));
        ScStatus status=ScJobQuery(s->job,&info);
        if(!status && info.state==SC32_JOB_FINISHED){
            s->finished=1;
            s->exit_code=info.exit_code;
        }
        else if(status<0){
            a_error(status,"查询终端作业失败");
            a_session_close(s);
        }
    }
    if(s->master){
        ScUiTerminalInfo dimensions;
        a_record(&dimensions,sizeof(dimensions));
        if(!s->finished && !s->closing && !ScUiTerminalQuery(s->terminal,&dimensions)
        && (s->columns!=dimensions.columns || s->rows!=dimensions.rows)){
            ScStatus status=ScTerminalResize(s->master,dimensions.columns,dimensions.rows);
            if(!status){
                s->columns=dimensions.columns;
                s->rows=dimensions.rows;
            }
            else if(status!=SC32_E_AGAIN)a_error(status,"终端尺寸同步失败");
        }
        if(!s->finished && !s->closing){
            if(s->pending_at==s->pending_bytes){
                s->pending_at=s->pending_bytes=0;
                ScUiTerminalTakeInput(s->terminal,s->pending,sizeof(s->pending),&s->pending_bytes);
            }
            if(s->pending_at<s->pending_bytes){
                ScIoResult io;
                a_record(&io,sizeof(io));
                ScStatus status=ScFileWrite(s->master,s->pending+s->pending_at,s->pending_bytes-s->pending_at,&io);
                if(io.transferred>s->pending_bytes-s->pending_at){
                    a_error(SC32_E_IO,"终端输入长度无效");
                    a_session_close(s);
                }
                else {
                    s->pending_at+=io.transferred;
                    input_blocked=status==SC32_E_AGAIN || status==SC32_E_BROKEN_PIPE;
                    if(status<0 && status!=SC32_E_AGAIN && status!=SC32_E_BROKEN_PIPE){
                        a_error(status,"终端输入失败");
                        a_session_close(s);
                    }
                    if(!status && s->pending_at<s->pending_bytes)more=1;
                }
            }
        }
        ScU32 portion=0;
        while(!s->read_eof && *budget && portion<4096){
            unsigned char bytes[1024];
            ScU32 take=*budget<sizeof(bytes)?*budget:(ScU32)sizeof(bytes);
            ScIoResult io;
            a_record(&io,sizeof(io));
            ScStatus status=ScFileRead(s->master,bytes,take,&io);
            if(io.transferred>take){
                a_error(SC32_E_IO,"终端输出长度无效");
                a_session_close(s);
                s->read_eof=1;
                break;
            }
            if(io.transferred){
                ScU32 accepted=0;
                ScStatus fed=ScUiTerminalFeed(s->terminal,bytes,io.transferred,&accepted);
                *budget-=io.transferred;
                portion+=io.transferred;
                if(fed<0 || accepted!=io.transferred){
                    a_error(fed<0?fed:SC_UI_E_IO,"终端画面更新失败，当前画面保留");
                    a_session_close(s);
                }
            }
            if(status==SC32_E_AGAIN)break;
            if(status<0){
                a_error(status,"终端输出失败");
                a_session_close(s);
                s->read_eof=1;
                break;
            }
            if(!io.transferred){
                s->read_eof=1;
                ScUiTerminalFinish(s->terminal);
                break;
            }
            more=1;
        }
    }
    if(s->finished && s->read_eof){
        if(s->master){
            ScStatus status=ScFileClose(s->master);
            if(status<0){
                a_error(status,"关闭终端流失败");
                return more;
            }
            s->master=0;
        }
        if(s->job){
            ScJobInfo info;
            a_record(&info,sizeof(info));
            ScStatus status=ScJobWait(s->job,0,&info);
            if(!status){
                s->job=0;
                s->exit_code=info.exit_code;
                snprintf(s->label,sizeof(s->label),"Shell %u  退出 %d",s->id,s->exit_code);
                ScUiText title=a_text(s->label);
                ScUiTabSetText(terminal_app.ui,terminal_app.tabs,s->page,&title);
            }
            else if(status<0)a_error(status,"领取终端退出状态失败");
        }
    }
    if(s->master && !s->finished){
        ScUiTerminalInfo info;
        a_record(&info,sizeof(info));
        if(!input_blocked && !ScUiTerminalQuery(s->terminal,&info) && (info.input_bytes || s->pending_at<s->pending_bytes))more=1;
    }
    return more;
}
static int a_jobs_step(void)
{
    ScU32 budget=16384;
    int more=0;
    ScU32 turns=terminal_app.session_count;
    while(turns-- && terminal_app.sessions){
        TerminalSession *s=terminal_app.cursor?terminal_app.cursor:terminal_app.sessions;
        TerminalSession *next=s->next?s->next:terminal_app.sessions;
        int only=next==s;
        more|=a_session_step(s,&budget);
        if(s->closing && !s->job && !s->master){
            if(!a_session_remove(s))more=1;
        }
        /* 下一节点在释放当前节点前取得；最后一页以后只能重新取活head。
         * 用本批真实数量预算轮转，不能在循环尾解引用已经free的start。 */
        terminal_app.cursor=only?terminal_app.sessions:next;
        if(!budget){
            return 1;
        }
    }
    return more;
}
