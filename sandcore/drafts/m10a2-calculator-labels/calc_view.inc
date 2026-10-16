static void calc_history_drop(void *item,void *context)
{(void)context;
    ScStringRelease(((CalcHistory *)item)->expression);
}
static ScStatus calc_history_notify(void)
{
    ScUiDataChange change;
    memset(&change,0,sizeof(change));
    change.size=sizeof(change);
    change.version=1;
    change.kind=SC_UI_DATA_RESET;
    change.revision=++calc.revision;
    return ScUiProviderNotify(calc.provider,&change);
}
static ScStatus calc_history_data(void *context,const ScUiDataRequest *request,ScUiDataReply *reply)
{
    (void)context;
    reply->revision=calc.revision;
    ScContainerInfo count;
    memset(&count,0,sizeof(count));
    count.size=sizeof(count);
    count.version=1;
    ScStatus s=ScArrayQuery(calc.history,&count);
    if(s<0)return s;
    if(request->operation==SC_UI_DATA_COUNT || request->operation==SC_UI_DATA_CHILD_COUNT){reply->total=count.length;
        return 0;
    }
    ScU32 index=request->operation==SC_UI_DATA_CHILD_AT?request->index:request->item-1;
    if(index>=count.length)return SC_UI_END;
    reply->item=index+1;
    reply->state=SC_UI_VISIBLE|SC_UI_ENABLED;
    if(request->operation==SC_UI_DATA_INDEX_OF){reply->total=index;
        return 0;
    }
    if(request->operation==SC_UI_DATA_CELL){CalcHistory row;
        s=ScArrayGet(calc.history,index,&row);
        if(s<0)return s;
        const char *text=request->field==1?row.decimal:request->field==2?row.hex:0;
        if(!text){s=ScStringCStr(row.expression,&text);
            if(s<0)return s;
        }
        /* ScArrayGet复制的数值副本不能作为借用返回，文字另从稳定
        * ScArrayView取得；任何成功修改后provider revision同时失效。 */
        if(request->field){ScView view;
            memset(&view,0,sizeof(view));
            view.size=sizeof(view);
            view.version=1;
            s=ScArrayView(calc.history,&view);
            if(s<0)return s;
            const CalcHistory *stored=(const CalcHistory *)view.data+index;
            text=request->field==1?stored->decimal:stored->hex;
        }
        reply->value.type=SC_VALUE_UTF8;
        reply->value.data=text;
        reply->value.bytes=(ScU32)strlen(text);
    }
    return 0;
}
static ScStatus calc_input_copy(ScString **out,const char **text,ScU32 *bytes)
{
    ScUiDocumentInfo info;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    ScStatus s=ScUiDocumentQuery(calc.document,&info);
    if(s<0)return s;
    if(info.bytes>4096)return SC_UI_E_RANGE;
    s=ScUiDocumentCopy(calc.document,0,info.bytes,out);
    if(s==0)s=ScStringCStr(*out,text);
    if(s==0)*bytes=info.bytes;
    return s;
}
static ScStatus calc_insert(const char *text)
{
    ScUiTextSelection selected;
    memset(&selected,0,sizeof(selected));
    selected.size=sizeof(selected);
    selected.version=1;
    ScStatus s=ScUiEditGetSelection(tool.ui,calc.input,&selected);
    if(s<0)return s;
    ScU32 start=selected.anchor<selected.caret?selected.anchor:selected.caret,end=selected.anchor>selected.caret?selected.anchor:selected.caret;
    ScUiText value=tool_text(text);
    ScUiDocumentInfo info;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    s=ScUiDocumentQuery(calc.document,&info);
    if(s==0 && info.bytes-(end-start)+value.bytes>4096)s=SC_UI_E_RANGE;
    if(s==0)s=ScUiDocumentReplace(calc.document,start,end-start,&value,SC_UI_REPLACE_NEW_UNDO);
    if(s==0){selected.anchor=selected.caret=start+value.bytes;
        selected.revision=0;
        s=ScUiEditSetSelection(tool.ui,calc.input,&selected);
    }
    return s;
}
static ScStatus calc_compute(void)
{
    ScString *expression=0;
    const char *text=0;
    ScU32 bytes=0;
    ScStatus s=calc_input_copy(&expression,&text,&bytes);
    if(s<0){ScStringRelease(expression);
        return s;
    }
    CalcResult result=calc_evaluate(text,bytes);
    calc.valid=result.status==0;
    if(result.status){char error[160];
        snprintf(error,sizeof(error),"%s（第%u字节）",calc_error_name(result.status),result.offset+1);
        ScUiText shown=tool_text(error);
        ScUiNodeSetText(tool.ui,calc.answer,&shown);
        shown=tool_text("输入保留；修改后按 Enter 重算");
        ScUiNodeSetText(tool.ui,calc.hex,&shown);
        tool_status(error);
    }else {calc.bits=result.bits;
        calc.last_valid=1;
        calc_format(result.bits,calc.decimal,calc.hexadecimal);
        ScUiText shown=tool_text(calc.decimal);
        ScUiNodeSetText(tool.ui,calc.answer,&shown);
        shown=tool_text(calc.hexadecimal);
        ScUiNodeSetText(tool.ui,calc.hex,&shown);
        CalcHistory entry;
        memset(&entry,0,sizeof(entry));
        entry.expression=expression;
        strcpy(entry.decimal,calc.decimal);
        strcpy(entry.hex,calc.hexadecimal);
        entry.bits=result.bits;
        ScContainerInfo count;
        memset(&count,0,sizeof(count));
        count.size=sizeof(count);
        count.version=1;
        s=ScArrayQuery(calc.history,&count);
        if(s==0)s=ScArrayInsert(calc.history,count.length,&entry,1);
        if(s==0){expression=0;
            calc_history_notify();
            ScUiViewEnsureItem(tool.ui,calc.table,count.length+1);
        }
        tool_status("精确有符号32位整数；结果可以跨应用复制");
    }
    ScStringRelease(expression);
    return s;
}
static ScStatus calc_clear(void)
{
    ScUiDocumentInfo info;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    ScStatus s=ScUiDocumentQuery(calc.document,&info);
    ScUiText empty=tool_text("");
    if(s==0)s=ScUiDocumentReplace(calc.document,0,info.bytes,&empty,SC_UI_REPLACE_NEW_UNDO);
    calc.valid=0;
    ScUiText ready=tool_text("就绪");
    ScUiNodeSetText(tool.ui,calc.answer,&ready);
    ScUiNodeSetText(tool.ui,calc.hex,&empty);
    return s;
}
static ScStatus calc_command(ScU32 command)
{
    static const char *keys[]={"(",")","AC","Back","%","/","7","8","9","A","B","*","4","5","6","C","D","-","1","2","3","E","F","+","0","0x","+/-","ANS","=","Copy"};
    if(command>=CALC_KEY_FIRST && command<CALC_KEY_FIRST+30){ScU32 key=command-CALC_KEY_FIRST;
        if(key==2)return calc_clear();
        if(key==3)command=CALC_BACKSPACE;
        else if(key==26)command=CALC_NEGATE;
        else if(key==27)command=CALC_ANS;
        else if(key==28)command=CALC_EVALUATE;
        else if(key==29)command=TOOLS_COPY;
        else return calc_insert(keys[key]);
    }
    if(command==TOOLS_EXIT){tool.quit=1;
        return ScUiContextStop(tool.ui,0);
    }
    if(command==CALC_EVALUATE)return calc_compute();
    if(command==CALC_CLEAR)return calc_clear();
    if(command==TOOLS_COPY || command==CALC_COPY_HEX)return calc.valid?tool_copy(command==TOOLS_COPY?calc.decimal:calc.hexadecimal):SC_UI_E_INVALID;
    if(command==CALC_UNDO)return ScUiDocumentUndo(calc.document);
    if(command==CALC_REDO)return ScUiDocumentRedo(calc.document);
    if(command==CALC_ANS)return calc.last_valid?calc_insert(calc.decimal):SC_UI_E_INVALID;
    if(command==CALC_NEGATE){ScString *input=0;
        const char *text=0;
        ScU32 bytes=0;
        ScStatus s=calc_input_copy(&input,&text,&bytes);
        ScString *candidate=0;
        if(s==0)s=ScStringCreate("-(",2,&candidate);
        if(s==0)s=ScStringAppend(candidate,text,bytes);
        if(s==0)s=ScStringAppend(candidate,")",1);
        const char *z=0;
        if(s==0)s=ScStringCStr(candidate,&z);
        if(s==0 && strlen(z)>4096)s=SC_UI_E_RANGE;
        if(s==0){ScUiText value=tool_text(z);
            s=ScUiDocumentReplace(calc.document,0,bytes,&value,SC_UI_REPLACE_NEW_UNDO);
        }
        ScStringRelease(candidate);
        ScStringRelease(input);
        return s;
    }
    if(command==CALC_BACKSPACE){ScUiTextSelection selected;
        memset(&selected,0,sizeof(selected));
        selected.size=sizeof(selected);
        selected.version=1;
        ScStatus s=ScUiEditGetSelection(tool.ui,calc.input,&selected);
        if(s<0)return s;
        if(selected.anchor==selected.caret && selected.caret){ScU32 previous=0;
            s=ScUiDocumentBoundary(calc.document,selected.caret,SC_UI_BOUNDARY_GRAPHEME,-1,&previous);
            if(s==0){selected.anchor=previous;
                selected.revision=0;
                s=ScUiEditSetSelection(tool.ui,calc.input,&selected);
        }}
        return s<0?s:calc_insert("");
    }
    if(command==CALC_RECALL){ScUiSelectionInfo selected;
        memset(&selected,0,sizeof(selected));
        selected.size=sizeof(selected);
        selected.version=1;
        ScStatus s=ScUiSelectionQuery(calc.selection,&selected);
        if(s<0 || !selected.current)return s<0?s:SC_UI_E_INVALID;
        CalcHistory row;
        s=ScArrayGet(calc.history,selected.current-1,&row);
        const char *z=0;
        if(s==0)s=ScStringCStr(row.expression,&z);
        if(s==0){ScUiDocumentInfo info;
            memset(&info,0,sizeof(info));
            info.size=sizeof(info);
            info.version=1;
            s=ScUiDocumentQuery(calc.document,&info);
            if(s==0){ScUiText value=tool_text(z);
                s=ScUiDocumentReplace(calc.document,0,info.bytes,&value,SC_UI_REPLACE_NEW_UNDO);
        }}
        return s;
    }
    if(command==TOOLS_ABOUT)return tool_message("Calculator 2","有符号32位整数表达式\n支持括号、优先级、十进制/0x十六进制、+ - * / %。\n十六进制是带符号幅值；无浮点或科学函数。");
    return SC_UI_E_NOT_SUPPORTED;
}
static ScStatus calc_input_event(ScUiContext *ui,const ScUiEvent *event,void *context)
{
    (void)ui;
    (void)context;
    if(event->type==SC_UI_EVENT_VALUE_CHANGED){calc.valid=0;tool_status("表达式已修改；按 Enter 重算后复制结果");}
    if(event->type==SC_UI_EVENT_KEY_DOWN && event->key=='\n'){ScStatus status=calc_command(CALC_EVALUATE);if(status<0)tool_error(status,"计算");
        return SC_UI_HANDLED;
    }
    if(event->type==SC_UI_EVENT_TEXT && (event->scalar=='c' || event->scalar=='C')){
        ScString *input=0;
        const char *text=0;
        ScU32 bytes=0;
        ScStatus s=calc_input_copy(&input,&text,&bytes);
        ScU32 hex=0;
        if(s==0){ScUiTextSelection selected;memset(&selected,0,sizeof(selected));selected.size=sizeof(selected);selected.version=1;
            ScU32 start=bytes;if(ScUiEditGetSelection(tool.ui,calc.input,&selected)==0 && selected.caret<=bytes)start=selected.caret;
            while(start && calc_digit(text[start-1])<16)start--;
            if(start && (text[start-1]=='x' || text[start-1]=='X') && start>=2 && text[start-2]=='0')hex=1;
        }
        ScStringRelease(input);
        if(!hex){calc_clear();
            return SC_UI_HANDLED;
    }}
    return 0;
}
static ScStatus calc_root_event(ScUiContext *ui,const ScUiEvent *event,void *context)
{
    (void)ui;(void)context;if(event->type!=SC_UI_EVENT_LAYOUT || !calc.result_row)return 0;
    ScUiNodeInfo info;memset(&info,0,sizeof(info));info.size=sizeof(info);info.version=1;
    if(ScUiNodeQuery(tool.ui,tool.root,&info)<0 || !tool.metrics.scale_percent)return 0;
    ScU32 width=(ScU32)info.rectangle.width*100/tool.metrics.scale_percent,height=(ScU32)info.rectangle.height*100/tool.metrics.scale_percent;
    ScU32 compact=height<450,history=width>=720 && height>=420,stamp=compact+history*2;
    if(calc.layout_stamp==stamp)return 0;
    calc.layout_stamp=stamp;
    tool_visible(calc.table,history);tool_visible(calc.history_splitter,history);
    tool_visible(tool.toolbar,!compact);tool_visible(calc.hex,!compact);
    return tool_layout(calc.result_row,SC_UI_VBOX,SC_UI_AUTO,compact?40:68,0);
}
static ScStatus calc_create(void)
{
    memset(&calc,0,sizeof(calc));
    calc.revision=1;
    calc.layout_stamp=UINT_MAX;
    ScStatus s=tool_create("Calculator 2",860,680,calc_command,calc_root_event);
    if(s<0)return s;
    ScArrayDesc history;
    memset(&history,0,sizeof(history));
    history.size=sizeof(history);
    history.version=1;
    history.item_bytes=sizeof(CalcHistory);
    history.drop=calc_history_drop;
    s=ScArrayCreate(&history,&calc.history);
    ScUiNode body,left;
    if(s==0)s=tool_widget(tool.content,SC_UI_TEXTBOX,"",0,SC_UI_AUTO,40,0,&calc.input);
    if(s==0)s=tool_widget(tool.content,SC_UI_PANEL,"",0,SC_UI_AUTO,68,0,&calc.result_row);
    if(s==0)s=tool_layout(calc.result_row,SC_UI_VBOX,SC_UI_AUTO,68,0);
    if(s==0)s=tool_widget(calc.result_row,SC_UI_LABEL,"就绪",0,SC_UI_AUTO,36,0,&calc.answer);
    if(s==0)s=tool_widget(calc.result_row,SC_UI_LABEL,"",0,SC_UI_AUTO,24,0,&calc.hex);
    if(s==0)s=ScUiNodeSetFont(tool.ui,calc.answer,SC_UI_FONT_HEADING);
    if(s==0)s=tool_widget(tool.content,SC_UI_PANEL,"",0,SC_UI_AUTO,SC_UI_AUTO,1,&body);
    if(s==0)s=tool_widget(body,SC_UI_SCROLL,"",0,SC_UI_AUTO,SC_UI_AUTO,1,&left);
    if(s==0)s=tool_layout(left,SC_UI_SCROLL_LAYOUT,SC_UI_AUTO,SC_UI_AUTO,1);
    if(s==0)s=tool_widget(left,SC_UI_PANEL,"",0,SC_UI_AUTO,SC_UI_AUTO,1,&calc.keyboard);
    if(s==0)s=tool_layout(calc.keyboard,SC_UI_VBOX,SC_UI_AUTO,180,1);
    if(s==0)s=tool_widget(body,SC_UI_SPLITTER,"",0,6,SC_UI_AUTO,0,&calc.history_splitter);
    if(s==0)s=tool_widget(body,SC_UI_TABLE,"",0,290,SC_UI_AUTO,0,&calc.table);
    static const char *labels[]={"(",")","AC","退格","%","/","7","8","9","A","B","×","4","5","6","C","D","-","1","2","3","E","F","+","0","0x","±","ANS","=","复制"};
    for(ScU32 row=0;s==0 && row<5;row++){ScUiNode bar;
        s=tool_widget(calc.keyboard,SC_UI_PANEL,"",0,SC_UI_AUTO,SC_UI_AUTO,1,&bar);
        for(ScU32 column=0;s==0 && column<6;column++){ScUiNode button;
            ScU32 index=row*6+column;
            s=tool_widget(bar,SC_UI_BUTTON,labels[index],CALC_KEY_FIRST+index,SC_UI_AUTO,SC_UI_AUTO,1,&button);
    }}
    ScUiDocumentDesc document;
    memset(&document,0,sizeof(document));
    document.size=sizeof(document);
    document.version=1;
    document.undo_budget_bytes=262144;
    document.text=tool_text("");
    if(s==0)s=ScUiDocumentCreate(&document,&calc.document);
    if(s==0)s=ScUiEditBind(tool.ui,calc.input,calc.document);
    ScUiProviderDesc provider;
    memset(&provider,0,sizeof(provider));
    provider.size=sizeof(provider);
    provider.version=1;
    provider.procedure=calc_history_data;
    if(s==0)s=ScUiProviderCreate(&provider,&calc.provider);
    if(s==0)s=ScUiSelectionCreate(calc.provider,SC_UI_SELECTION_ALLOW_EMPTY,&calc.selection);
    if(s==0)s=ScUiViewBind(tool.ui,calc.table,calc.provider,calc.selection);
    ScUiColumnDesc columns[3];
    memset(columns,0,sizeof(columns));
    const char *titles[]={"表达式","十进制","十六进制"};
    for(ScU32 i=0;i<3;i++){columns[i].size=sizeof(columns[i]);
        columns[i].version=1;
        columns[i].column=i+1;
        columns[i].field=i;
        columns[i].title=tool_text(titles[i]);
        columns[i].width=i?130:220;
        columns[i].min_width=60;
        columns[i].max_width=1600;
    }
    if(s==0)s=ScUiTableSetColumns(tool.ui,calc.table,columns,3);
    ToolMenuEntry file[]={{"计算",CALC_EVALUATE,'\n',0},{"清除",CALC_CLEAR,27,0},{"关闭",TOOLS_EXIT,'w',SC32_MOD_CONTROL}};
    ToolMenuEntry edit[]={{"复制结果",TOOLS_COPY,'c',SC32_MOD_CONTROL},{"复制十六进制",CALC_COPY_HEX,0,0},{"撤销",CALC_UNDO,'z',SC32_MOD_CONTROL},
        {"重做",CALC_REDO,'y',SC32_MOD_CONTROL},{"恢复所选历史",CALC_RECALL,0,0},{"插入ANS",CALC_ANS,0,0}};
    ToolMenuEntry help[]={{"数值范围与语法",TOOLS_ABOUT,SC32_KEY_F1,0}};
    if(s==0)s=tool_menu_part("计算",file,3);
    if(s==0)s=tool_menu_part("编辑",edit,6);
    if(s==0)s=tool_menu_part("帮助",help,1);
    ScUiMenu *toolbar=0;
    if(s==0)s=ScUiMenuCreate(&toolbar);
    for(ScU32 i=0;s==0 && i<3;i++)s=tool_menu_item(toolbar,file+i,0);
    if(s==0)s=ScUiToolbarBind(tool.ui,tool.toolbar,toolbar);
    ScUiMenuRelease(toolbar);
    if(s==0)s=tool_listen(calc.input,SC_UI_TARGET_PHASE,calc_input_event,0);
    if(s==0)s=ScUiFocus(tool.ui,calc.input);
    tool_status("输入表达式后按 Enter；所有中间值均须在有符号32位范围内");
    return s;
}
