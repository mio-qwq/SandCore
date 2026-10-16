/* 在sc32_core.c中包含；沿同一caller_valid/input/output/publish合同。
 * 新selector007不是旧0x71的忽略寄存器扩展，不接受任意固定目标路径。
 * session_config_path / userspace_config / auth_can_manage来自对应可信头。 */
static int settings_native_status(int raw)
{
    if(!raw)return 0;
    if(raw==FS_N_NOT_FOUND)return SC32_E_NOT_FOUND;
    if(raw==FS_N_ACCESS)return SC32_E_ACCESS;
    if(raw==FS_N_MEMORY)return SC32_E_MEMORY;
    if(raw==FS_N_BUSY)return SC32_E_BUSY;
    if(raw==FS_N_NOT_SUPPORTED)return SC32_E_NOT_SUPPORTED;
    if(raw==FS_N_IO)return SC32_E_IO;
    if(raw==FS_N_STALE)return SC32_E_STALE;
    return SC32_E_INVALID;
}
static int settings_source_path(int pid,u32 pointer,char out[64])
{
    char absolute[128];u32 i;
    for(i=0;i<sizeof(absolute);i++){
        if(pointer>0xffffffffu-i || paging_user_copy(TASK(pid).pd,pointer+i,absolute+i,1,0))return SC32_E_INVALID;
        if(!absolute[i])break;
    }
    if(i==sizeof(absolute) || !i || absolute[0]!='/')return SC32_E_INVALID;
    return fs_normalize(absolute,out)>0?0:SC32_E_INVALID;
}
static int settings_legacy_status(int raw)
{
    if(!raw)return 0;
    if(raw==-5)return SC32_E_ACCESS;
    if(raw==-4)return SC32_E_MEMORY;
    if(raw==-2 || raw==-3)return SC32_E_IO;
    if(raw==-6)return SC32_E_BUSY;
    return SC32_E_INVALID;
}
static int settings_config_apply(int pid,u32 request,u32 output)
{
    Sc32ConfigurationRequest q;int status=sc32_core_input(pid,request,&q,sizeof(q));if(status)return status;
    status=sc32_core_output(pid,output,sizeof(ScConfigurationResult));if(status)return status;
    for(u32 i=0;i<8;i++)if(q.reserved[i])return SC32_E_INVALID;
    if(q.kind>1 || q.action>2 || q.flags&~3u || q.flags==3u)return SC32_E_INVALID;
    if(q.action==SC32_CONFIG_VALIDATE && (q.flags || q.expected_target_revision))return SC32_E_INVALID;
    if(q.action==SC32_CONFIG_SAVE && q.flags!=SC32_CONFIG_EXPECT_ABSENT && q.flags!=SC32_CONFIG_EXPECT_EXACT)return SC32_E_INVALID;
    if((q.flags==SC32_CONFIG_EXPECT_EXACT)!=!!q.expected_target_revision)return SC32_E_INVALID;
    if(q.action==SC32_CONFIG_RELOAD && (q.candidate || q.expected_candidate_revision || q.flags==SC32_CONFIG_EXPECT_ABSENT))return SC32_E_INVALID;
    if(q.action!=SC32_CONFIG_RELOAD && (!q.candidate || !q.expected_candidate_revision))return SC32_E_INVALID;
    ScConfigurationResult info;clear(&info,sizeof(info));info.size=sizeof(info);info.version=1;
    info.kind=q.kind;info.action=q.action;info.session=session_task(pid);info.flags=SC32_CONFIG_CREDENTIALS_UNCHANGED;
    char fixed[64],candidate[64];u32 target[8],source[8];clear(target,sizeof(target));clear(source,sizeof(source));
    if(q.action==SC32_CONFIG_SAVE && !auth_can_manage(pid)){status=SC32_E_ACCESS;goto finished;}
    if(session_config_path(q.kind?"ENV.CFG":"USER.CFG",fixed)<0){status=SC32_E_ACCESS;goto finished;}
    if(q.action!=SC32_CONFIG_RELOAD){
        status=settings_source_path(pid,q.candidate,candidate);if(status)goto finished;
        if(!fs_access(pid,candidate,FS_ACCESS_READ)){status=SC32_E_ACCESS;goto finished;}
        int native=fs_path_query(pid,candidate,source);if(native){status=settings_native_status(native);info.raw=native;goto finished;}
        if(source[1]!=1 || source[2]>=4096 || source[7]!=q.expected_candidate_revision){status=source[7]!=q.expected_candidate_revision?SC32_E_STALE:SC32_E_INVALID;goto finished;}
        info.candidate_revision=source[7];
    }
    if(q.action!=SC32_CONFIG_VALIDATE){
        int native=fs_path_query(pid,fixed,target);
        if(native && native!=FS_N_NOT_FOUND){status=settings_native_status(native);info.raw=native;goto finished;}
        if(q.flags==SC32_CONFIG_EXPECT_ABSENT && !native){status=SC32_E_EXISTS;goto finished;}
        if(q.flags==SC32_CONFIG_EXPECT_EXACT && (native || target[7]!=q.expected_target_revision)){status=SC32_E_STALE;goto finished;}
        if(!native && target[1]!=1){status=SC32_E_INVALID;goto finished;}
        info.target_revision=native?0:target[7];
    }
    /* 全部路径是可信局部副本；caller_valid保证IF=0，下面没有回到R3、
     * 没有新用户回调或主动STI。真实消费者在此同步读/解析同候选并
     * 沿旧权限保存/激活，另一任务不能在revision核验后替换候选。
     * 源/目标均未被本接口预写，语法/权限/OOM失败保留原运行配置。 */
    info.raw=userspace_config((int)q.kind,q.action==SC32_CONFIG_RELOAD?0:candidate,(int)q.action);
    status=settings_legacy_status(info.raw);
    if(!status){
        info.flags|=SC32_CONFIG_VALIDATED;
        if(q.action!=SC32_CONFIG_VALIDATE)info.flags|=SC32_CONFIG_ACTIVE;
        if(q.action==SC32_CONFIG_SAVE)info.flags|=SC32_CONFIG_DURABLE;
        if(q.action!=SC32_CONFIG_VALIDATE){int native=fs_path_query(pid,fixed,target);
            if(!native)info.target_revision=target[7];else {info.target_revision=0;status=settings_native_status(native);}}
    }else if(q.action==SC32_CONFIG_SAVE && info.raw==-3){
        /* 旧v5超级块提交IO报错可能不明。原COW数据仍保留、profile未
         * 进入新值；不能把内存回退说成磁盘已经回滚，也不能自动重放。 */
        info.flags|=SC32_CONFIG_UNCERTAIN;
    }
finished:
    info.status=status;int published=publish(pid,output,&info,sizeof(info));return published?published:status;
}
/* sc32_core_call switch增加：
 * case SC32_C_CONFIG_APPLY:return settings_config_apply(pid,request,output);
 * 并包含userspace.h及本目录私有WIRE；主task.c默认core路由不变。 */
