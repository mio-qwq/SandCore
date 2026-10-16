static void install_result(InstallItem *item,ScU32 state,ScStatus code,const char *detail)
{
    item->state=state;
    item->error=code;
    snprintf(item->detail,sizeof(item->detail),"%s",detail);
    install_changed();
}
static ScStatus install_permissions(InstallItem *item)
{
    IoPin local;
    memset(&local,0,sizeof(local));
    IoPin *pin=installer.writer.candidate.lease?&installer.writer.candidate:&local;
    ScStatus s=0;
    if(!pin->lease){ScPathInfo target;
        INIT(target);
        s=ScPathQuery(item->destination,&target);
        if(s<0)return s;
        if(target.generation!=item->result_revision)return SC32_E_STALE;
        s=pin_open(item->destination,item->kind,item->kind==SC32_PATH_FILE?SC32_LEASE_WRITE_EXCLUSIVE:SC32_LEASE_NAMESPACE,target.generation,0,pin);
    }
    if(s==0)s=ScPathPermissions(item->destination,installer.final_uid,installer.final_gid,installer.final_mode);
    if(s==0){ScPathInfo final;
        INIT(final);
        s=ScPathQuery(item->destination,&final);
        if(s==0 && (final.rw!=installer.final_mode || (installer.final_uid!=-2 && final.uid!=installer.final_uid) || (installer.final_gid!=-2 && final.gid!=installer.final_gid)))s=SC32_E_STALE;
        if(s==0)item->result_revision=final.generation;
    }
    pin_release(&local,0);
    return s;
}
static ScStatus install_parent_step(InstallItem *item)
{
    if(!installer.create_parents)return 0;
    ScU32 length=(ScU32)strlen(item->destination);
    while(installer.parent_cursor<length){ScU32 i=++installer.parent_cursor;
        if(i==length || item->destination[i]=='/'){
            /* 文件只建父目录；目录目标本体交给主job按所选mode提交。 */
            if(i==length)return 0;
            if(i>=sizeof(installer.parent_path))return SC32_E_RANGE;
            memcpy(installer.parent_path,item->destination,i);
            installer.parent_path[i]=0;
            if(!*installer.parent_path)continue;
            ScPathInfo meta;
            INIT(meta);
            ScStatus s=ScPathQuery(installer.parent_path,&meta);
            if(s==0){if(meta.kind!=SC32_PATH_DIRECTORY)return SC32_E_INVALID;
                continue;
            }
            if(s!=SC32_E_NOT_FOUND)return s;
            s=io_start_directory(&installer.writer,installer.parent_path,23);
            if(s<0)return s;
            installer.writer.hold=0;
            installer.child_directory=1;
            installer.phase=3;
            return SC32_PENDING;
        }
    }return 0;
}
static ScStatus installer_step(void)
{
    if(installer.reporting){ScStatus s=io_step(&installer.report);
        if(s==SC32_PENDING)return s;
        status(s==0?"逐项结果已持久保存":"结果报告保存失败；安装文件不会被重抄");
        io_dispose(&installer.report);
        installer.reporting=0;
        return s;
    }
    if(!installer.phase)return 0;
    if(installer.cursor>=installer.count){installer.phase=0;
        installer.checking=0;
        status("逐项处理结束；查看结果列与详情，未将部分失败称成功");
        return 0;
    }
    InstallItem *item=installer.items+installer.cursor;
    ScStatus s=0;
    if(installer.cancel && installer.phase!=3){installer.phase=0;
        installer.checking=0;
        status("已停止；既有文件和源文件保留，已持久项见结果");
        return 0;
    }
    if(installer.phase==1){
        if(item->state==I2_UNCERTAIN){installer.cursor++;
            return SC32_PENDING;
        }
        s=install_preflight_item(item);
        if(s<0)install_result(item,I2_FAILED,s,"预检失败；尚未改动该目标");
        else install_changed();
        installer.cursor++;
        if(installer.cursor==installer.count){installer.checking=0;
            if(!installer.run_after_check){installer.phase=0;
                status("只读预检结束；未写入文件");
                return 0;
            }installer.cursor=0;
            installer.phase=2;
        }
        return SC32_PENDING;
    }
    if(installer.phase==2){
        if(item->state==I2_DONE || item->state==I2_REUSED || item->state==I2_UNCERTAIN || item->state==I2_FAILED){installer.cursor++;
            return SC32_PENDING;
        }
        if(item->state==I2_CONTENT){s=install_permissions(item);
            install_result(item,s==0?I2_DONE:I2_FAILED,s,s==0?"权限已核实；来源保留":"正文已持久，权限未完成；可仅重试权限");
            installer.cursor++;
            return s<0?s:SC32_PENDING;
        }
        s=install_parent_step(item);
        if(s==SC32_PENDING)return s;
        if(s==0)s=item->kind==SC32_PATH_DIRECTORY?io_start_directory(&installer.writer,item->destination,installer.final_mode):io_start_copy(&installer.writer,item->destination,item->source,item->source_revision,installer.final_mode);
        if(s<0){install_result(item,I2_FAILED,s,"创建候选失败；未覆盖目标，来源保留");
            io_dispose(&installer.writer);
            installer.cursor++;
            installer.parent_cursor=0;
            return s;
        }
        installer.writer.hold=1;
        installer.phase=3;
        item->state=I2_COPYING;
        install_changed();
        return SC32_PENDING;
    }
    if(installer.phase==3){
        if(installer.cancel)installer.writer.cancel=1;
        s=io_step(&installer.writer);
        ScUiProgressSet(app.ui,installer.progress,installer.writer.offset,installer.writer.length,SC_UI_PROGRESS_DETERMINATE);
        if(s==SC32_PENDING)return s;
        if(installer.child_directory){
            if(s<0){install_result(item,installer.writer.uncertain?I2_UNCERTAIN:I2_FAILED,s,"父目录创建失败；已完成父目录保留，来源保留");
                installer.cursor++;
                installer.parent_cursor=0;
            }
            io_dispose(&installer.writer);
            installer.child_directory=0;
            installer.phase=2;
            return s<0?s:SC32_PENDING;
        }
        if(s<0){install_result(item,installer.writer.uncertain?I2_UNCERTAIN:installer.cancel?I2_CANCELLED:I2_FAILED,s,"复制未确认成功；来源保留，不自动覆盖重试");
        }
        else {item->durable=1;
            item->result_revision=installer.writer.observation.revision;
            item->state=I2_CONTENT;
            s=install_permissions(item);
            install_result(item,s==0?I2_DONE:I2_FAILED,s,s==0?"正文DURABLE，uid/gid/rw已核实；来源保留":"正文已持久，权限失败；重试只处理权限");
        }
        io_dispose(&installer.writer);
        installer.cursor++;
        installer.parent_cursor=0;
        installer.phase=2;
        return s<0?s:SC32_PENDING;
    }
    return SC32_E_INVALID;
}
static ScStatus install_retry(void)
{
    if(installer.phase)return SC32_E_BUSY;
    InstallItem *item=install_current();
    if(!item)return SC32_E_NOT_FOUND;
    if(item->state==I2_UNCERTAIN)return message("结果不明","请先检查卷/真实目标；此入口不会自动重放不明提交。");
    if(item->state!=I2_FAILED && item->state!=I2_CANCELLED)return SC32_E_INVALID;
    ScStatus s=install_options();
    if(s<0)return s;
    s=install_preflight_item(item);
    if(s<0)return s;
    installer.cursor=(ScU32)(item-installer.items);
    installer.cancel=0;
    installer.parent_cursor=0;
    installer.phase=2;
    status(item->durable?"只重试该已持久目标的权限；不复制来源":"已重新预检；从选中项继续处理剩余计划");
    return 0;
}
static ScStatus install_report(ScString **out)
{
    ScString *body=0;
    ScStatus s=ScStringCreate("文件安装逐项结果\n",(ScU32)strlen("文件安装逐项结果\n"),&body);
    for(ScU32 i=0;s==0 && i<installer.count;i++){InstallItem *row=installer.items+i;
        s=ScStringAppendFormat(body,"%u. %s -> %s\n%s; error=%d; source-revision=%u; target-revision=%u; durable=%u\n%s\n",i+1,row->source,row->destination,install_state(row->state),row->error,row->source_revision,row->result_revision,row->durable,row->detail);
    }
    if(s<0){ScStringRelease(body);
        return s;
    }*out=body;
    return 0;
}
