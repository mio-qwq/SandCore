static ScLeaseControl f2_lease_control(const ScLeaseInfo *info)
{
    ScLeaseControl control;memset(&control,0,sizeof(control));control.size=sizeof(control);control.version=1;
    control.lease=info->lease;control.generation=info->generation;control.operation_sequence=info->operation_sequence;return control;
}
static ScStatus f2_release_step(ScLeaseInfo *info)
{
    if(!info->lease)return 0;
    ScLeaseControl control=f2_lease_control(info);ScStatus s;
    if(info->state==SC32_LEASE_PENDING_STATE)s=ScFileLeaseAdvance(&control,info);
    else s=ScFileLeaseRelease(&control,info);
    if(s==SC32_E_STALE){info->lease=0;return 0;}
    if(s<0)return s;
    if(info->state==SC32_LEASE_RELEASED){info->lease=0;return 0;}
    return SC32_PENDING;
}
static ScStatus f2_temporary(const char *path,char out[F2_PATH])
{
    const char *slash=strrchr(path,'/');if(!slash)return SC32_E_INVALID;
    ScU32 parent=(ScU32)(slash-path)+1;if(parent>=64 || f2.recovery_serial==UINT_MAX)return SC32_E_RANGE;
    ScU32 serial=++f2.recovery_serial,width=64-parent;if(width>8)width=8;
    static const char digits[]="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    memcpy(out,path,parent);
    for(ScU32 i=0;i<width;i++){out[parent+width-i-1]=digits[serial%36];serial/=36;}
    out[parent+width]=0;
    if(!f2_compare_text(out,path))return SC32_E_EXISTS;
    return 0;
}
static ScStatus f2_writer_begin(const char *path,ScString *body,ScU32 expected,ScU32 purpose)
{
    if(f2.writer.active || f2.writer.candidate.lease || f2.writer.target.lease
        || f2.writer.file || f2.writer.target_file)return SC_UI_E_BUSY;
    const char *z=0;ScStatus s=ScStringCStr(body,&z);if(s<0)return s;
    ScContainerInfo bytes;memset(&bytes,0,sizeof(bytes));bytes.size=sizeof(bytes);bytes.version=1;
    s=ScStringQuery(body,&bytes);if(s<0)return s;
    if(purpose==F2_WRITE_BOOKMARKS && bytes.length>1048576)return SC_UI_E_RANGE;
    ScPathInfo metadata;s=f2_query(path,&metadata);
    if(expected){if(s<0)return s;if(metadata.kind!=SC32_PATH_FILE || metadata.generation!=expected)return SC32_E_STALE;}
    else if(s!=SC32_E_NOT_FOUND)return s<0?s:SC32_E_EXISTS;
    s=ScStringRetain(body);if(s<0)return s;
    F2Writer *writer=&f2.writer;memset(writer,0,sizeof(*writer));writer->active=1;writer->purpose=purpose;
    writer->expected_revision=expected;writer->body=body;writer->total=bytes.length;writer->serial=f2.bookmark_serial;
    writer->candidate.size=sizeof(writer->candidate);writer->candidate.version=1;
    writer->target.size=sizeof(writer->target);writer->target.version=1;strcpy(writer->path,path);
    (void)z;f2_status(purpose==F2_WRITE_BOOKMARKS?"正在持久保存书签":"正在创建不可见文件候选");return 0;
}
static ScStatus f2_writer_step(void)
{
    F2Writer *writer=&f2.writer;if(!writer->active)return 0;
    ScStatus s=0;ScLeaseOpenDesc open;memset(&open,0,sizeof(open));open.size=sizeof(open);open.version=1;
    open.object_kind=SC32_PATH_FILE;open.lease_kind=SC32_LEASE_WRITE_EXCLUSIVE;
    if(writer->cancel && writer->stage<8){
        writer->result=SC32_E_CANCELLED;
        if(writer->candidate.lease && writer->candidate.state==SC32_LEASE_PENDING_STATE){ScLeaseControl c=f2_lease_control(&writer->candidate);
            s=ScFileLeaseCancel(&c,&writer->candidate);if(s<0)writer->result=s;}
        writer->stage=8;
    }
    if(writer->stage==0){
        if(writer->expected_revision){open.path=writer->path;open.expected_revision=writer->expected_revision;open.open_flags=SC32_FILE_WRITE;
            s=ScFileLeaseOpenPinned(&open,&writer->target);if(s==0)writer->target_file=writer->target.file;}
        if(s==0)writer->stage=1;
    }else if(writer->stage==1){
        /* 很深的目录可能只余一个临时名字字符；限制冲突重试工作量，
         * 无可用私有名字时明确BUSY，不能因进位溢出连续空转数十亿次。 */
        if(writer->attempts==64)s=SC32_E_BUSY;
        else {writer->attempts++;s=f2_temporary(writer->path,writer->temporary);}
        if(s==SC32_E_EXISTS)return 0;
        if(s==0){open.path=writer->temporary;open.open_flags=SC32_FILE_READ|SC32_FILE_WRITE;open.create_mode=3;
            s=ScFileLeaseCreatePinned(&open,&writer->candidate);}
        if((s==SC32_E_EXISTS || s==SC32_E_BUSY) && writer->attempts<64)return 0;
        if(s==0){writer->file=writer->candidate.file;writer->stage=2;}
    }else if(writer->stage==2){
        const char *z=0;s=ScStringCStr(writer->body,&z);
        if(s==0 && writer->offset<writer->total){ScU32 count=writer->total-writer->offset;if(count>4096)count=4096;
            ScIoResult result;memset(&result,0,sizeof(result));result.size=sizeof(result);result.version=1;
            s=ScFileWrite(writer->file,z+writer->offset,count,&result);
            if(result.transferred>count)s=SC_UI_E_INVALID;
            else writer->offset+=result.transferred;
            if(s==0 && !result.transferred)s=SC32_E_IO;}
        if(s==0 && writer->offset==writer->total)writer->stage=3;
    }else if(writer->stage==3){s=ScFileFlush(writer->file);if(s==0)writer->stage=4;
    }else if(writer->stage==4){ScLeaseControl c=f2_lease_control(&writer->candidate);s=ScFileLeaseQuery(&c,&writer->candidate);if(s==0)writer->stage=5;
    }else if(writer->stage==5){
        ScLeasePublishDesc publish;memset(&publish,0,sizeof(publish));publish.size=sizeof(publish);publish.version=1;
        publish.source_lease=writer->candidate.lease;publish.source_generation=writer->candidate.generation;
        publish.expected_source_object=writer->candidate.object_id;publish.expected_source_revision=writer->candidate.revision;
        publish.target_path=writer->path;publish.final_mode=3;
        publish.flags=SC32_LEASE_SET_MODE|(writer->expected_revision?SC32_LEASE_EXPECT_EXACT:SC32_LEASE_EXPECT_ABSENT);
        if(writer->expected_revision){publish.target_lease=writer->target.lease;publish.target_generation=writer->target.generation;
            publish.expected_target_object=writer->target.object_id;publish.expected_target_revision=writer->target.revision;}
        s=ScFileLeasePublish(&publish,&writer->candidate);if(s>=0)writer->stage=6;
    }else if(writer->stage==6){
        if(writer->candidate.state==SC32_LEASE_PENDING_STATE){ScLeaseControl c=f2_lease_control(&writer->candidate);
            s=ScFileLeaseAdvance(&c,&writer->candidate);}
        if(s==0){
            if(writer->candidate.state!=SC32_LEASE_COMMITTED || !(writer->candidate.flags&SC32_LEASE_FLAG_DURABLE))s=SC_SHELL_E_UNKNOWN;
            else {writer->committed=1;writer->stage=8;
                if(writer->purpose==F2_WRITE_BOOKMARKS){f2.bookmark_revision=writer->candidate.revision;f2.saved_serial=writer->serial;
                    f2.bookmark_save_pending=f2.bookmark_serial!=f2.saved_serial;f2_status("书签已持久保存");}
                else {f2.refresh_pending=1;strcpy(f2.pending_file,writer->path);f2_status("文件已持久创建");}}
        }
    }else if(writer->stage==8){s=f2_release_step(&writer->candidate);if(s==0)writer->stage=9;
    }else if(writer->stage==9){s=f2_release_step(&writer->target);if(s==0)writer->stage=10;
    }else if(writer->stage==10){
        if(writer->file){s=ScFileClose(writer->file);if(s==0)writer->file=0;}
        if(s==0 && writer->target_file){s=ScFileClose(writer->target_file);if(s==0)writer->target_file=0;}
        if(s==0){ScStringRelease(writer->body);ScStatus result=writer->result;memset(writer,0,sizeof(*writer));
            if(result<0)return result;
            return 0;}
    }
    if(s<0){
        if(writer->stage>=8){
            /* 已知提交与清理失败分别保留；不把清理错误反解成磁盘回滚。
             * 无法主动释放的能力仍由进程退出回收，不重复发布或截断来源。 */
            ScStringRelease(writer->body);writer->body=0;writer->active=0;return s;
        }
        writer->result=s;writer->stage=8;f2.bookmark_save_pending=0;
        f2_status((writer->candidate.flags&SC32_LEASE_FLAG_UNCERTAIN)?"提交结果不确定；保留来源，请检查目标":"操作失败；当前书签和来源保留");
    }
    return 0;
}
static ScStatus f2_bookmark_add_path(const char *path)
{
    if(f2.bookmark_count>=F2_HISTORY_FIRST-F2_BOOKMARK_FIRST)return SC_UI_E_RANGE;
    for(ScU32 i=0;i<f2.bookmark_count;i++)if(!f2_compare_text(f2.bookmarks[i].path,path))return SC_UI_E_EXISTS;
    ScStatus s=f2_reserve((void **)&f2.bookmarks,&f2.bookmark_capacity,f2.bookmark_count+1,sizeof(F2Bookmark));
    if(s==0)strcpy(f2.bookmarks[f2.bookmark_count++].path,path);
    return s;
}
static ScStatus f2_bookmarks_load(void)
{
    ScPathInfo metadata;ScStatus s=f2_query(f2.bookmark_path,&metadata);
    if(s==SC32_E_NOT_FOUND)return 0;
    if(s<0)return s;
    if(metadata.kind!=SC32_PATH_FILE || metadata.bytes>1048576)return SC_UI_E_RANGE;
    ScFileOpenDesc desc;memset(&desc,0,sizeof(desc));desc.size=sizeof(desc);desc.version=1;
    desc.path=f2.bookmark_path;desc.flags=SC32_FILE_READ;ScFile file=0;s=ScFileOpen(&desc,&file);if(s<0)return s;
    char *bytes=malloc(metadata.bytes+1);ScConfig *config=0;ScU32 offset=0;
    if(!bytes)s=SC_UI_E_MEMORY;
    while(s==0 && offset<metadata.bytes){ScIoResult result;memset(&result,0,sizeof(result));result.size=sizeof(result);result.version=1;
        ScU32 count=metadata.bytes-offset;if(count>4096)count=4096;s=ScFileRead(file,bytes+offset,count,&result);
        if(result.transferred>count)s=SC_UI_E_INVALID;else offset+=result.transferred;
        if(s==0 && !result.transferred)s=SC32_E_IO;}
    ScFileInfo after;memset(&after,0,sizeof(after));after.size=sizeof(after);after.version=1;
    if(s==0)s=ScFileQuery(file,&after);
    ScStatus closed=ScFileClose(file);if(s==0 && closed<0)s=closed;
    if(s==0 && after.revision!=metadata.generation)s=SC32_E_STALE;
    if(s==0){bytes[offset]=0;s=ScConfigCreate(&config);}
    ScParseIssue issue;memset(&issue,0,sizeof(issue));issue.size=sizeof(issue);issue.version=1;
    if(s==0)s=ScConfigParse(config,bytes,offset,0,&issue);
    ScValue count;memset(&count,0,sizeof(count));count.size=sizeof(count);count.version=1;
    if(s==0)s=ScConfigGet(config,"bookmarks","count",&count);
    if(s==0 && (count.type!=SC_VALUE_U32 || count.lo>metadata.bytes/3))s=SC_UI_E_INVALID;
    F2Bookmark *original=f2.bookmarks;ScU32 original_count=f2.bookmark_count,original_capacity=f2.bookmark_capacity;
    if(s==0){f2.bookmarks=0;f2.bookmark_count=f2.bookmark_capacity=0;}
    else {ScConfigRelease(config);free(bytes);return s;}
    for(ScU32 i=0;s==0 && i<count.lo;i++){
        char key[24],path[F2_PATH],candidate[F2_PATH];snprintf(key,sizeof(key),"B%u",i);
        ScValue value;memset(&value,0,sizeof(value));value.size=sizeof(value);value.version=1;s=ScConfigGet(config,"bookmarks",key,&value);
        if(s==0 && (value.type!=SC_VALUE_UTF8 || value.bytes>64 || !value.bytes || memchr(value.data,0,value.bytes)))s=SC_UI_E_INVALID;
        if(s==0){memcpy(candidate,value.data,value.bytes);candidate[value.bytes]=0;s=f2_absolute(candidate,"/",path);}
        if(s==0){s=f2_bookmark_add_path(path);if(s==SC_UI_E_EXISTS)s=0;}
    }
    if(s<0){free(f2.bookmarks);f2.bookmarks=original;f2.bookmark_count=original_count;f2.bookmark_capacity=original_capacity;}
    else {free(original);f2.bookmark_revision=metadata.generation;}
    ScConfigRelease(config);free(bytes);return s;
}
static ScStatus f2_bookmarks_save(void)
{
    if(f2.writer.active)return SC_UI_E_BUSY;
    ScConfig *config=0;ScString *body=0;ScStatus s=ScConfigCreate(&config);
    ScValue value;memset(&value,0,sizeof(value));value.size=sizeof(value);value.version=1;
    value.type=SC_VALUE_U32;value.lo=f2.bookmark_count;if(s==0)s=ScConfigSet(config,"bookmarks","count",&value);
    for(ScU32 i=0;s==0 && i<f2.bookmark_count;i++){
        char key[24];snprintf(key,sizeof(key),"B%u",i);memset(&value,0,sizeof(value));value.size=sizeof(value);value.version=1;
        value.type=SC_VALUE_UTF8;value.data=f2.bookmarks[i].path;value.bytes=(ScU32)strlen(f2.bookmarks[i].path);
        s=ScConfigSet(config,"bookmarks",key,&value);
    }
    if(s==0)s=ScConfigSerialize(config,SC_CONFIG_SORT_KEYS,&body);
    if(s==0)s=f2_writer_begin(f2.bookmark_path,body,f2.bookmark_revision,F2_WRITE_BOOKMARKS);
    ScStringRelease(body);ScConfigRelease(config);f2.bookmark_save_pending=0;return s;
}
static ScStatus f2_paths(void)
{
    ScU32 length=0;ScStatus s=ScSessionPath("FILES2.CFG",f2.bookmark_path,F2_PATH,&length);if(s<0)return s;
    if(!strcmp(f2.bookmark_path,"/SYS/FILES2.CFG"))strcpy(f2.bookmark_path,"/HOME/.FILES2.CFG");
    s=ScSessionPath("FILEJOBS",f2.recovery_directory,F2_PATH,&length);if(s<0)return s;
    if(!strcmp(f2.recovery_directory,"/SYS/FILEJOBS"))strcpy(f2.recovery_directory,"/HOME/.FILEJOBS");
    return 0;
}
static ScStatus f2_recovery(void)
{
    ScPathInfo metadata;ScStatus s=f2_query(f2.recovery_directory,&metadata);
    if(s==SC32_E_NOT_FOUND){s=ScPathCreate(f2.recovery_directory,SC32_PATH_DIRECTORY,3);
        if(s==SC32_E_EXISTS){s=f2_query(f2.recovery_directory,&metadata);if(s==0 && metadata.kind!=SC32_PATH_DIRECTORY)s=SC32_E_INVALID;}}
    else if(s==0 && metadata.kind!=SC32_PATH_DIRECTORY)s=SC32_E_INVALID;
    if(s<0)return s;
    if(f2.recovery_serial==UINT_MAX)return SC32_E_RANGE;
    int bytes=snprintf(f2.recovery_path,F2_PATH,"%s/%X-%X.REC",f2.recovery_directory,f2.identity.task_generation,++f2.recovery_serial);
    return bytes<0 || bytes>64?SC32_E_RANGE:0;
}
