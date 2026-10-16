static U read32(const unsigned char *p){
    return (U)p[0]|((U)p[1]<<8)|((U)p[2]<<16)|((U)p[3]<<24);
}
static int same_name(const char *a,const char *b){
    if(*a=='/')a++;
    if(*b=='/')b++;
    for(;;){
        U x=(unsigned char)*a++,y=(unsigned char)*b++;
        if(x>='a'&&x<='z')x-=32;
        if(y>='a'&&y<='z')y-=32;
        if(x!=y)return 0;
        if(!x)return 1;
    }
}
static const char *leaf(const char *s){
    const char *p=strrchr(s,'/');
    return p?p+1:s;
}
static ScStatus join(char out[68],const char *dir,const char *name)
{
    int n=snprintf(out,68,"%s%s%s",dir,dir[strlen(dir)-1]=='/'?"":"/",name);
    return n<0||n>64?SC32_E_RANGE:0;
}
static void diagnostic(const char *line,U generation)
{
    const char *p=0;
    if(!strncmp(line,"SCCC ERROR ",11))p=line+11;
    else if(!strncmp(line,"SCLD ERROR ",11))p=line+11;
    else return;
    const char *colon=strchr(p,':');
    if(!colon||colon-p>64||colon==p)return;
    char name[68];
    memcpy(name,p,(U)(colon-p));
    name[colon-p]=0;
    char *end=0;
    unsigned long row=strtoul(colon+1,&end,10);
    if(end==colon+1||*end!=':'||row>0xffffffffu)return;
    char absolute[68];
    if(path(name,absolute)<0)return;
    for(U i=0;i<st.build.input_count;i++)if(same_name(absolute,st.build.inputs[i].object))strcpy(absolute,st.build.inputs[i].member.path);
    if(st.error_count==st.error_capacity){
        U next=st.error_capacity?st.error_capacity*2u:16u;
        if(next>1048576u/sizeof(StDiagnostic))return;
        StDiagnostic *q=realloc(st.errors,next*sizeof(*q));
        if(!q)return;
        st.errors=q;
        st.error_capacity=next;
    }
    StDiagnostic *d=st.errors+st.error_count++;
    memset(d,0,sizeof(*d));
    d->id=++st.diag_serial;
    d->generation=generation;
    d->line=(U)row;
    strcpy(d->path,absolute);
    U message_bytes=(U)strlen(end+1);
    if(message_bytes>=sizeof(d->message))message_bytes=sizeof(d->message)-1u;
    while(message_bytes&&(((unsigned char)end[1+message_bytes]&192u)==128u))message_bytes--;
    memcpy(d->message,end+1,message_bytes);
    d->message[message_bytes]=0;
    if(ScUtf8Validate(d->message,message_bytes,0)<0)strcpy(d->message,"[diagnostic text contains non-UTF8 bytes]");
    st.diag_revision++;
    ScUiDataChange change;
    rec(&change,sizeof(change));
    change.kind=SC_UI_DATA_RESET;
    change.revision=st.diag_revision;
    ScUiProviderNotify(st.diag_provider,&change);
}
static void output_append(U build,const void *bytes,U count)
{
    if(!count||!st.output_document)return;
    ScUiDocumentInfo info;
    rec(&info,sizeof(info));
    if(ScUiDocumentQuery(st.output_document,&info)<0)return;
    ScString *clean=0;
    ScStatus s=ScStringCreate("",0,&clean);
    const unsigned char *p=bytes;
    U at=0;
    while(s>=0&&at<count){
        U before=at,scalar=0;
        if(p[at]<128u){
            scalar=p[at++];
            if(!scalar)scalar='?';
        }
        else if(ScUtf8Decode(bytes,count,&at,&scalar)<0){
            at=before+1u;
            scalar=0xfffdu;
        }
        unsigned char encoded[4];
        U n=0;
        s=ScUtf8Encode(scalar,encoded,sizeof(encoded),&n);
        if(s>=0)s=ScStringAppend(clean,encoded,n);
    }
    ScView v;
    rec(&v,sizeof(v));
    if(s>=0)s=ScStringView(clean,&v);
    if(s>=0 && info.bytes+v.bytes>1048576u){
        ScString *prefix=0;
        U take=info.bytes<8192u?info.bytes:8192u;
        if(ScUiDocumentCopy(st.output_document,0,take,&prefix)>=0){
            ScView first;
            rec(&first,sizeof(first));
            if(ScStringView(prefix,&first)>=0){
                U remove=0;
                while(remove<first.bytes&&((const char *)first.data)[remove]!='\n')remove++;
                if(remove<first.bytes)remove++;
                else{
                    remove=first.bytes;
                    while(remove&&(((const unsigned char *)first.data)[remove-1u]&192u)==128u)remove--;
                }
                if(remove==first.bytes)ScUiDocumentBoundary(st.output_document,remove,SC_UI_BOUNDARY_GRAPHEME,-1,&remove);
                ScUiText empty=text("");
                ScUiDocumentReplace(st.output_document,0,remove,&empty,SC_UI_REPLACE_NO_UNDO);
                info.bytes-=remove;
            }
            ScStringRelease(prefix);
        }
    }
    if(s>=0){
        ScUiText t={
            sizeof(t),1,v.data,v.bytes
        };
        s=ScUiDocumentReplace(st.output_document,info.bytes,0,&t,SC_UI_REPLACE_NO_UNDO);
    }
    if(s<0)status("输出显示资源不足；构建退出码仍独立检查");
    ScStringRelease(clean);
    (void)build;
}
static void child_line(StChild *c,U stream,U build)
{
    U n=c->used[stream];
    if(!n)return;
    c->pending[stream][n]=0;
    if(build)diagnostic(c->pending[stream],st.build.generation);
    output_append(build,c->pending[stream],n);
    c->used[stream]=0;
}
static void child_step(StChild *c,U build)
{
    if(!c->capture.job){
        if(c->artifact.live)pin_close(&c->artifact);
        return;
    }
    if(c->cancel==1u){
        ScJobCancel(c->capture.job);
        c->cancel=2u;
    }
    for(U channel=0;channel<2;channel++)if(!c->eof[channel]){
        unsigned char chunk[4096];
        ScIoResult r;
        rec(&r,sizeof(r));
        ScFile file=channel?c->capture.error:c->capture.output;
        ScStatus s=ScFileRead(file,chunk,sizeof(chunk),&r);
        if(s==SC32_E_AGAIN)continue;
        if(s<0){
            c->eof[channel]=1;
            c->exit_code=1;
            c->cancel=1;
            child_line(c,channel,build);
            continue;
        }
        if(!r.transferred){
            c->eof[channel]=1;
            child_line(c,channel,build);
            continue;
        }
        for(U i=0;i<r.transferred;i++){
            if(c->used[channel]==sizeof(c->pending[channel])-2u){
                /* 长诊断行显示切块，保留UTF8边界，余字节留下一次行缓冲。 */
                U end=c->used[channel];
                while(end&&((unsigned char)c->pending[channel][end-1u]&192u)==128u)end--;
                if(end&&((unsigned char)c->pending[channel][end-1u]>=192u))end--;
                char tail[4];
                U remain=c->used[channel]-end;
                memcpy(tail,c->pending[channel]+end,remain);
                c->used[channel]=end;
                child_line(c,channel,build);
                memcpy(c->pending[channel],tail,remain);
                c->used[channel]=remain;
            }
            c->pending[channel][c->used[channel]++]=(char)(chunk[i]?chunk[i]:'?');
            if(chunk[i]=='\n')child_line(c,channel,build);
        }
    }
    ScJobInfo info;
    rec(&info,sizeof(info));
    ScStatus s=ScJobQuery(c->capture.job,&info);
    if(s<0){
        c->cancel=1;
        c->exit_code=1;
        return;
    }
    if(info.state==SC32_JOB_FINISHED){
        c->ended=1;
        if(!c->exit_code)c->exit_code=info.exit_code;
    }
    if(!c->ended||!c->eof[0]||!c->eof[1])return;
    s=ScJobClose(c->capture.job);
    if(s==SC32_E_BUSY)return;
    if(s<0)c->exit_code=1;
    if(c->capture.output){
        ScFileClose(c->capture.output);
        c->capture.output=0;
    }
    if(c->capture.error){
        ScFileClose(c->capture.error);
        c->capture.error=0;
    }
    c->capture.job=0;
    if(c->artifact.live)pin_close(&c->artifact);
    if(!build){
        status(c->exit_code?"程序已结束（非零退出码）":"程序已结束");
        refresh();
    }
}
static ScStatus spawn(StChild *c,const char *command,U compiler)
{
    if(c->capture.job)return SC32_E_BUSY;
    char prior[256];
    U n=0;
    ScStatus saved=0,s=0,restored=0;
    U had=0;
    if(compiler){
        saved=ScProcessEnvironment("SCCC_INCLUDE",prior,sizeof(prior),&n);
        if(saved>=0)had=1;
        else if(saved!=SC32_E_NOT_FOUND)return saved;
        s=ScProcessSetEnvironment("SCCC_INCLUDE",st.includes[0]?st.includes:"",st.includes[0]?0:1);
        if(s<0)return s;
    }
    memset(c,0,sizeof(*c));
    rec(&c->capture,sizeof(c->capture));
    s=ScProcessSpawnCapture(command,&c->capture);
    /* Spawn已在内核同步复制环境。恢复即使spawn失败也执行；恢复失败
     * 取消已创建的真实job，不让环境污染获得一个成功构建资格。 */
    if(compiler)restored=ScProcessSetEnvironment("SCCC_INCLUDE",had?prior:"",had?0:1);
    if(restored<0){
        if(c->capture.job)c->cancel=1;
        return restored;
    }
    return s;
}
static ScStatus namespace_pin(StPin *p)
{
    if(p->live)return SC32_E_BUSY;
    ScPathInfo path_info;
    rec(&path_info,sizeof(path_info));
    ScStatus s=ScPathQuery("/SYS/INC",&path_info);
    if(s<0)return s;
    if(path_info.kind!=SC32_PATH_DIRECTORY)return SC32_E_INVALID;
    ScDirectory directory=0;
    s=ScDirectoryOpen("/SYS/INC",&directory);
    if(s<0)return s;
    /* 先交给构建清理状态机持有。后续失败不能丢掉还未关闭的目录，
     * 尤其不能把一次返回BUSY的Close当成已回收。 */
    rec(&p->info,sizeof(p->info));
    p->info.directory=directory;
    p->live=1;
    ScDirectoryEntry row;
    ScDirectoryPage page;
    rec(&page,sizeof(page));
    s=ScDirectoryNext(directory,&row,1,&page);
    if(s<0)return s;
    /* revision与全局目录epoch是两种身份。只读Acquire固定实际子树，
     * 仍允许编译器独立读头文件；META的OpenPinned不能代替这份合同。 */
    ScLeaseAcquireDesc d;
    ScLeaseInfo acquired;
    rec(&d,sizeof(d));
    rec(&acquired,sizeof(acquired));
    d.kind=SC32_LEASE_NAMESPACE;
    d.directory=directory;
    d.expected_revision=path_info.generation;
    d.expected_epoch=page.generation;
    d.flags=SC32_LEASE_READ_ONLY_NAMESPACE;
    s=ScFileLeaseAcquire(&d,&acquired);
    if(s>=0)p->info=acquired;
    return s;
}
static ScStatus inputs_current(void)
{
    StBuild *b=&st.build;
    if(b->epoch!=st.epoch)return SC32_E_STALE;
    for(U i=0;i<b->input_count;i++){
        StInput *in=b->inputs+i;
        ScPathInfo q;
        rec(&q,sizeof(q));
        ScStatus s=ScPathQuery(in->member.path,&q);
        if(s<0)return s;
        if(q.generation!=in->generation)return SC32_E_STALE;
        StDoc *d=document(in->doc_id);
        if(d&&d->revision!=in->doc_revision)return SC32_E_STALE;
    }
    StDoc *manifest=document(b->manifest_doc);
    if(manifest&&manifest->revision!=b->manifest_document_revision)return SC32_E_STALE;
    for(U i=0;i<b->dependency_count;i++){
        ScPathInfo q;
        rec(&q,sizeof(q));
        ScStatus s=ScPathQuery(b->dependencies[i].path,&q);
        if(s<0)return s;
        if(q.generation!=b->dependencies[i].revision)return SC32_E_STALE;
    }
    ScPathInfo q;
    rec(&q,sizeof(q));
    ScStatus s=ScPathQuery(st.manifest_path,&q);
    return s<0?s:q.generation!=b->manifest_revision?SC32_E_STALE:0;
}
static ScStatus outputs_current(void)
{
    const char *suffix[3]={
        ".sdbg",".map",""
    };
    for(U i=0;i<3;i++){
        char name[68];
        int n=snprintf(name,sizeof(name),"%s%s",st.output_path,suffix[i]);
        if(n<0||(U)n>=sizeof(name))return SC32_E_RANGE;
        ScPathInfo q;
        rec(&q,sizeof(q));
        ScStatus s=ScPathQuery(name,&q);
        if(s<0)return s;
        if(q.kind!=SC32_PATH_FILE||q.generation!=st.output_revisions[i])return SC32_E_STALE;
    }
    return 0;
}
static void build_fail(ScStatus s){
    if(!st.build.error)st.build.error=s;
    st.build.cancel=1;
    st.success=0;
    status("构建未完成，旧产物不取得运行资格");
}
static ScStatus build_begin(void)
{
    StBuild *b=&st.build;
    if(b->phase||st.io||st.running.capture.job)return SC32_E_BUSY;
    if(!st.member_count)return SC32_E_INVALID;
    U next=b->generation+1u;
    if(!next)return SC32_E_RANGE;
    free(b->inputs);
    free(b->dependencies);
    memset(b,0,sizeof(*b));
    b->generation=next;
    b->epoch=st.epoch;
    b->phase=S_BUILD_SAVE;
    b->input_count=st.member_count;
    b->inputs=calloc(b->input_count,sizeof(*b->inputs));
    if(!b->inputs){
        b->phase=0;
        return SC32_E_MEMORY;
    }
    for(U i=0;i<b->input_count;i++)b->inputs[i].member=st.members[i];
    st.success=0;
    st.error_count=0;
    st.diag_revision++;
    ScUiDataChange change;
    rec(&change,sizeof(change));
    change.kind=SC_UI_DATA_RESET;
    change.revision=st.diag_revision;
    ScUiProviderNotify(st.diag_provider,&change);
    ScUiDocumentInfo q;
    rec(&q,sizeof(q));
    ScUiDocumentQuery(st.output_document,&q);
    ScUiText empty=text("");
    ScUiDocumentReplace(st.output_document,0,q.bytes,&empty,SC_UI_REPLACE_NO_UNDO);
    ScUiTabSelect(st.ui,st.out_tabs,st.output_page);
    status("保存本次工程输入…");
    refresh();
    return 0;
}
static ScStatus sco_sources(StInput *in)
{
    StBuild *b=&st.build;
    ScPathInfo q;
    rec(&q,sizeof(q));
    ScStatus s=ScPathQuery(in->object,&q);
    if(s<0)return s;
    unsigned char head[96];
    U n=0;
    s=ScPathRead(in->object,0,head,96,&n);
    if(s<0||n!=96u)return s<0?s:SC32_E_IO;
    if(memcmp(head,"SCO1MIO\0",8)||read32(head+8)!=1u||read32(head+24)!=q.bytes)return SC32_E_INVALID;
    U files=read32(head+52),count=read32(head+56),strings=read32(head+68),stringbytes=read32(head+72);
    if(files>q.bytes||count>(q.bytes-files)/16u||strings>q.bytes||stringbytes>q.bytes-strings)return SC32_E_INVALID;
    /* 按实际source-file表枚举依赖身份；项目外非系统头必须显式
     * 加为header后重新构建，不能悄悄漏跟踪。 */
    for(U i=0;i<count;i++){
        unsigned char row[16];
        n=0;
        s=ScPathRead(in->object,files+i*16u,row,16,&n);
        if(s<0||n!=16u)return SC32_E_IO;
        U offset=read32(row);
        if(offset>=stringbytes)return SC32_E_INVALID;
        /* SCCC把旧RESOLVE的根内名原样写入SCO，不能把它再当cwd相对名。
         * 先补显式根，再沿原path正规化；后面的工程/namespace/revision检查不变。
         * name+1仍有67B，读取上限同时给根斜杠和字符串终止留足空间。 */
        char name[68];name[0]='/';
        U take=stringbytes-offset;
        if(take>67u)take=67u;
        n=0;
        s=ScPathRead(in->object,strings+offset,name+1,take,&n);
        if(s<0||!memchr(name+1,0,n)||!name[1])return SC32_E_INVALID;
        char absolute[68];
        s=path(name,absolute);
        if(s<0)return s;
        char system[10];
        for(U k=0;k<9u;k++){
            int c=(unsigned char)absolute[k];
            system[k]=(char)(c>='a'&&c<='z'?c-32:c);
            if(!c)break;
        }
        system[9]=0;
        if(!strcmp(system,"/SYS/INC/")){
            U found=0;
            for(U k=0;k<b->dependency_count;k++)if(same_name(absolute,b->dependencies[k].path))found=1;
            if(!found){
                if(b->dependency_count==b->dependency_capacity){
                    U cap=b->dependency_capacity?b->dependency_capacity*2u:16u;
                    if(cap>0x1000000u/sizeof(StDependency))return SC32_E_RANGE;
                    StDependency *p=realloc(b->dependencies,cap*sizeof(*p));
                    if(!p)return SC32_E_MEMORY;
                    b->dependencies=p;
                    b->dependency_capacity=cap;
                }
                ScPathInfo actual;
                rec(&actual,sizeof(actual));
                s=ScPathQuery(absolute,&actual);
                if(s<0)return s;
                StDependency *p=b->dependencies+b->dependency_count++;
                strcpy(p->path,absolute);
                p->revision=actual.generation;
            }
            continue;
        }
        U found=0;
        for(U k=0;k<b->input_count;k++)if(same_name(absolute,b->inputs[k].member.path))found=1;
        if(!found){
            error(SC32_E_STALE,"编译器使用了工程外头文件；请加入工程后重新构建");
            return SC32_E_STALE;
        }
    }
    return 0;
}
static ScStatus publication(U exporting,const char *directory)
{
    StBuild *b=&st.build;
    if(b->filejob)return SC32_E_BUSY;
    const char *suffix[3]={
        ".sdbg",".map",""
    };
    for(U i=0;i<3;i++){
        int n=snprintf(b->src[i],68,"%s%s",exporting?st.output_path:b->candidate,suffix[i]);
        if(n<0||n>64)return SC32_E_RANGE;
        if(exporting){
            char base[68];
            n=snprintf(base,68,"%s%s",leaf(st.output_path),suffix[i]);
            if(n<0||n>64)return SC32_E_RANGE;
            ScStatus s=join(b->dst[i],directory,base);
            if(s<0)return s;
        }
        else{
            n=snprintf(b->dst[i],68,"%s%s",st.output_path,suffix[i]);
            if(n<0||n>64)return SC32_E_RANGE;
        }
        if(same_name(b->src[i],b->dst[i]))return SC32_E_INVALID;
        ScPathInfo src,dst;
        rec(&src,sizeof(src));
        rec(&dst,sizeof(dst));
        ScStatus s=ScPathQuery(b->src[i],&src);
        if(s<0)return s;
        s=ScPathQuery(b->dst[i],&dst);
        if(s<0&&s!=SC32_E_NOT_FOUND)return s;
        ScShellFileItem *item=b->items+i;
        rec(item,sizeof(*item));
        item->item=i+1u;
        item->source=b->src[i];
        item->destination=b->dst[i];
        item->kind=SC32_PATH_FILE;
        item->expected_source_revision=src.generation;
        item->expected_destination_revision=s>=0?dst.generation:0;
        item->expected_destination_kind=s>=0?dst.kind:0;
        item->flags=SC_SHELL_ITEM_EXPECT_SOURCE|(s>=0?SC_SHELL_ITEM_EXPECT_DESTINATION:SC_SHELL_ITEM_EXPECT_ABSENT);
    }
    ScShellFileJobDesc d;
    rec(&d,sizeof(d));
    d.operation=SC_SHELL_COPY;
    d.items=b->items;
    d.count=3;
    d.conflict_policy=SC_SHELL_REPLACE;
    d.final_mode=23;
    ScStatus s=ScShellFileOperationBegin(&d,&b->filejob);
    if(s>=0){
        b->publishing=exporting?2u:1u;
        ScShellProgress(st.ui,st.root,b->filejob,&b->progress);
    }
    return s;
}
static ScStatus publish_begin(const char *directory,U exporting)
{
    if(st.build.phase||st.io)return SC32_E_BUSY;
    ScStatus s=inputs_current();
    if(s>=0)s=outputs_current();
    if(s<0||!st.success)return s<0?s:SC32_E_STALE;
    return publication(exporting,directory);
}
static void publication_step(void)
{
    StBuild *b=&st.build;
    if(!b->filejob)return;
    ScShellFileOperationStep(b->filejob,0);
    if(b->progress)ScShellDialogStep(b->progress);
    ScShellFileProgress q;
    rec(&q,sizeof(q));
    ScStatus s=ScShellFileOperationQuery(b->filejob,&q);
    if(s<0){
        build_fail(s);
        return;
    }
    if(q.state<SC_SHELL_JOB_FINISHED)return;
    U exporting=b->publishing==2u;
    if(q.state!=SC_SHELL_JOB_FINISHED||q.items_done!=3u||q.items_failed||q.items_skipped)s=SC32_E_IO;
    for(U i=0;i<3&&s>=0;i++){
        ScShellFileResult r;
        rec(&r,sizeof(r));
        s=ScShellFileOperationResult(b->filejob,i,&r);
        if(s>=0&&(r.status<0||!(r.flags&SC_SHELL_DURABLE)||!r.destination_revision))s=r.status<0?r.status:SC32_E_IO;
        if(s>=0&&!exporting)st.output_revisions[i]=r.destination_revision;
    }
    ScShellDialogClose(b->progress);
    b->progress=0;
    ScShellFileOperationClose(b->filejob);
    b->filejob=0;
    b->publishing=0;
    if(!exporting){
        if(s>=0)s=inputs_current();
        ScPathInfo out;
        rec(&out,sizeof(out));
        if(s>=0)s=ScPathQuery(st.output_path,&out);
        if(s>=0)s=outputs_current();
        if(s>=0){
            st.success=1;
            st.success_epoch=st.epoch;
            st.success_generation=b->generation;
            st.output_revision=out.generation;
            status("本次SCX/SCL、map与SDBG已发布");
        }
        else build_fail(s);
        b->phase=S_BUILD_CLEAN;
        b->next=0;
    }
    else{
        if(s<0)error(s,"发布未完整完成");
        else status("三份本次构建产物已发布");
    }
    refresh();
}
static void build_step(void)
{
    StBuild *b=&st.build;
    publication_step();
    if(!b->phase)return;
    child_step(&b->child,1);
    if(st.epoch!=b->epoch)b->cancel=1;
    if(b->cancel&&b->phase!=S_BUILD_CLEAN){
        if(b->child.capture.job){
            b->child.cancel=1;
            return;
        }
        if(b->filejob){
            ScShellFileOperationCancel(b->filejob);
            return;
        }
        if(st.io){
            st.io->cancel=1;
            return;
        }
        b->phase=S_BUILD_CLEAN;
        b->next=0;
        if(!b->error)b->error=SC32_E_CANCELLED;
    }
    if(b->phase==S_BUILD_SAVE){
        if(st.io)return;
        for(U i=0;i<b->input_count;i++){
            StDoc *d=document_path(b->inputs[i].member.path);
            if(!d)continue;
            ScUiDocumentInfo q;
            rec(&q,sizeof(q));
            ScStatus s=ScUiDocumentQuery(d->text,&q);
            if(s<0){
                build_fail(s);
                return;
            }
            if((q.flags&SC_UI_DOCUMENT_DIRTY)||!d->disk_revision){
                s=io_save(d,d->path);
                if(s<0)build_fail(s);
                return;
            }
        }
        StDoc *manifest=document_path(st.manifest_path);
        if(manifest){
            ScUiDocumentInfo q;
            rec(&q,sizeof(q));
            ScStatus s=ScUiDocumentQuery(manifest->text,&q);
            if(s<0){
                build_fail(s);
                return;
            }
            if((q.flags&SC_UI_DOCUMENT_DIRTY)||!manifest->disk_revision){
                s=io_save(manifest,manifest->path);
                if(s<0)build_fail(s);
                return;
            }
        }
        ScProcessRef self;
        rec(&self,sizeof(self));
        ScStatus s=ScProcessSelf(&self);
        if(s<0){
            build_fail(s);
            return;
        }
        char dir[32];
        snprintf(dir,sizeof(dir),"S%08X",self.pid*65537u+self.generation*40503u+b->generation);
        s=join(b->directory,st.workspace,dir);
        if(s>=0)s=ScPathCreate(b->directory,SC32_PATH_DIRECTORY,23);
        if(s<0){
            build_fail(s);
            return;
        }
        s=join(b->candidate,b->directory,leaf(st.output_path));
        if(s>=0)s=join(b->response,b->directory,"OBJECTS.RSP");
        if(s<0||strlen(b->candidate)>59u){
            build_fail(SC32_E_RANGE);
            return;
        }
        s=namespace_pin(&b->system_headers);
        if(s<0){
            build_fail(s);
            return;
        }
        b->phase=S_BUILD_PIN;
        b->next=0;
        return;
    }
    if(b->phase==S_BUILD_PIN){
        if(b->next<b->input_count){
            StInput *in=b->inputs+b->next;
            ScPathInfo q;
            rec(&q,sizeof(q));
            ScStatus s=ScPathQuery(in->member.path,&q);
            if(s>=0)s=pin_open(&in->pin,in->member.path,q.generation,0);
            if(s<0){
                build_fail(s);
                return;
            }
            in->generation=q.generation;
            StDoc *d=document_path(in->member.path);
            if(d){
                in->doc_id=d->id;
                in->doc_revision=d->revision;
            }
            char object[24];
            snprintf(object,sizeof(object),"U%08X.SCO",b->next);
            s=join(in->object,b->directory,object);
            if(s<0){
                build_fail(s);
                return;
            }
            b->next++;
            return;
        }
        ScPathInfo q;
        rec(&q,sizeof(q));
        ScStatus s=ScPathQuery(st.manifest_path,&q);
        if(s>=0)s=pin_open(&b->manifest,st.manifest_path,q.generation,0);
        if(s<0){
            build_fail(s);
            return;
        }
        b->manifest_revision=q.generation;
        StDoc *manifest=document_path(st.manifest_path);
        if(manifest){
            b->manifest_doc=manifest->id;
            b->manifest_document_revision=manifest->revision;
        }
        b->phase=S_BUILD_COMPILE;
        b->next=0;
        return;
    }
    if(b->phase==S_BUILD_COMPILE){
        if(b->child.capture.job)return;
        if(b->linking){
            if(b->child.exit_code){
                build_fail(SC32_E_FAILED);
                return;
            }
            ScStatus s=sco_sources(b->inputs+b->next);
            if(s<0){
                build_fail(s);
                return;
            }
            b->next++;
            b->linking=0;
        }
        while(b->next<b->input_count&&!b->inputs[b->next].member.source)b->next++;
        if(b->next==b->input_count){
            b->phase=S_BUILD_LINK;
            b->linking=0;
            return;
        }
        StInput *in=b->inputs+b->next;
        char cmd[384];
        int n=snprintf(cmd,sizeof(cmd),"%s %s %s --object",st.compiler,in->member.path,in->object);
        if(n<0||(U)n>=sizeof(cmd)){
            build_fail(SC32_E_RANGE);
            return;
        }
        output_append(1,cmd,(U)n);
        output_append(1,"\n",1);
        ScStatus s=spawn(&b->child,cmd,1);
        if(s<0){
            build_fail(s);
            return;
        }
        b->linking=1;
        return;
    }
    if(b->phase==S_BUILD_LINK){
        if(b->child.capture.job)return;
        if(b->linking){
            if(b->child.exit_code){
                build_fail(SC32_E_FAILED);
                return;
            }
            unsigned char head[36];
            U got=0;
            ScStatus s=ScPathRead(b->candidate,0,head,sizeof(head),&got);
            if(s<0||got<36u||memcmp(head,st.target_scl?"SCL1MIO\0":"SCX1MIO\0",8)){
                build_fail(SC32_E_INVALID);
                return;
            }
            b->phase=S_BUILD_SETTLE;
            b->next=0;
            return;
        }
        FILE *f=fopen(b->response,"wb");
        if(!f){
            build_fail(SC32_E_IO);
            return;
        }
        U sources=0;
        ScStatus s=0;
        for(U i=0;i<b->input_count;i++)if(b->inputs[i].member.source){
            sources++;
            const char *p=b->inputs[i].object;
            if(fwrite(p,1,strlen(p),f)!=strlen(p)||fwrite("\n",1,1,f)!=1)s=SC32_E_IO;
        }
        if(fclose(f))s=SC32_E_IO;
        if(!sources)s=SC32_E_INVALID;
        if(s<0){
            build_fail(s);
            return;
        }
        char cmd[384];
        int n=snprintf(cmd,sizeof(cmd),"%s %s %s %s @%s",st.linker,b->candidate,st.target_scl?"--scl":"--dynamic",st.manifest_path,b->response);
        if(n<0||(U)n>=sizeof(cmd)){
            build_fail(SC32_E_RANGE);
            return;
        }
        output_append(1,cmd,(U)n);
        output_append(1,"\n",1);
        s=spawn(&b->child,cmd,0);
        if(s<0){
            build_fail(s);
            return;
        }
        b->linking=1;
        return;
    }
    if(b->phase==S_BUILD_SETTLE){
        /* 只读namespace可能覆盖用户选定的正式输出父目录；发布前解除
         * 这些pin，前后重验所有revision，不给worker伪造namespace授权。 */
        if(b->next<b->input_count){
            ScStatus s=pin_close(&b->inputs[b->next].pin);
            if(s==SC32_PENDING)return;
            if(s<0){
                build_fail(s);
                return;
            }
            b->next++;
            return;
        }
        ScStatus s=pin_close(&b->manifest);
        if(s==SC32_PENDING)return;
        if(s>=0)s=pin_close(&b->system_headers);
        if(s==SC32_PENDING)return;
        if(s>=0)s=inputs_current();
        if(s>=0)s=publication(0,0);
        if(s<0){
            build_fail(s);
            return;
        }
        b->phase=S_BUILD_COPY;
        return;
    }
    if(b->phase==S_BUILD_CLEAN){
        /* 只有已确认关闭才推进。失败仍保留pin，下个安全点重试，
         * 不让下一次构建memset吞掉尚活的目录或lease引用。 */
        if(b->next<b->input_count){
            ScStatus s=pin_close(&b->inputs[b->next].pin);
            if(s){if(s<0&&!b->error)b->error=s;return;}
            b->next++;
            return;
        }
        ScStatus s=pin_close(&b->manifest);
        if(s){if(s<0&&!b->error)b->error=s;return;}
        s=pin_close(&b->system_headers);
        if(s){if(s<0&&!b->error)b->error=s;return;}
        b->phase=0;
        b->cancel=0;
        if(b->error){
            st.success=0;
            error(b->error,"构建结束：未取得当前产物资格");
        }
        refresh();
    }
}
static ScStatus run(U debug)
{
    if(!st.success||st.success_epoch!=st.epoch||st.build.phase||st.running.capture.job||st.running.artifact.live)return SC32_E_STALE;
    ScStatus s=inputs_current();
    if(s>=0)s=outputs_current();
    if(s<0){
        st.success=0;
        refresh();
        return s;
    }
    ScPathInfo q;
    rec(&q,sizeof(q));
    s=ScPathQuery(st.output_path,&q);
    if(s<0||q.generation!=st.output_revision)return SC32_E_STALE;
    StPin output;
    memset(&output,0,sizeof(output));
    s=pin_open(&output,st.output_path,q.generation,0);
    if(s<0)return s;
    char target[768],cmd[1024];
    if(st.target_scl){
        if(!st.debug_host[0]){
            pin_close(&output);
            return SC32_E_NOT_SUPPORTED;
        }
        snprintf(target,sizeof(target),"%s %s%s%s",st.debug_host,st.output_path,st.arguments[0]?" ":"",st.arguments);
    }
    else snprintf(target,sizeof(target),"%s%s%s",st.output_path,st.arguments[0]?" ":"",st.arguments);
    int n=snprintf(cmd,sizeof(cmd),"%s%s",debug?"/APPS/DEBUG2.SCX --launch ":"",target);
    if(n<0||(U)n>=sizeof(cmd))s=SC32_E_RANGE;
    else s=spawn(&st.running,cmd,0);
    if(s>=0){
        st.running.artifact=output;
        memset(&output,0,sizeof(output));
        status(debug?"Debug 2 正在拥有并暂停实际目标":"程序已启动，输出由本窗口接收");
    }
    else pin_close(&output);
    refresh();
    return s;
}
