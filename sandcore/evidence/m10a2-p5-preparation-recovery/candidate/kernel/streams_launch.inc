/* 在 streams.c 的私有 endpoint/attach 实现之后包含；新入口不建立 job。
 * 关闭 task_spawn 暂时继承的三条流发生在同一 IF=0 陷入内，子进程尚未
 * 得到调度，因此管道不会留下隐藏 writer，终端关闭也不会连带杀子。 */
int streams_launch_session(int parent,const char *path,const char *arguments)
{
    if(parent<=0 || parent!=task_pid() || !task_owned(parent)
        || TASK(parent).state!=1 || !path || !arguments)return -1;
    u32 identity[8];
    if(auth_info(parent,identity) || (int)identity[1]<0 || (int)identity[2]<0
        || identity[3]!=AUTH_NORMAL)return -5;
    session_t *scope=session_get(session_task(parent));
    if(!scope || scope->kind!=SESSION_NORMAL || scope->closing)return -5;
    char resolved[64];
    if(userspace_resolve(parent,path,resolved,sizeof(resolved))<0 || !resolved[0])return -1;
    int child=task_exec_scx_parts(resolved,arguments);
    if(child<1)return child;
    for(int i=0;i<3;i++)streams_close(child,i,0);
    return child;
}
