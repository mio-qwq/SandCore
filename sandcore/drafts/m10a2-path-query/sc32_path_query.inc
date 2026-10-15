/* 在sc32_file.c的path/publish/native_status之后包含；查询不初始化FD池。 */
static int path_query(int pid,u32 request,u32 output)
{
    u32 query[8];int status=sc32_core_input(pid,request,query,sizeof(query));
    if(status)return status;
    for(u32 i=3;i<8;i++)if(query[i])return SC32_E_INVALID;
    status=sc32_core_output(pid,output,sizeof(ScPathInfo));if(status)return status;
    char name[64];status=path(pid,query[2],name);if(status)return status;
    u32 raw[8];status=native_status(fs_path_query(pid,name,raw));if(status)return status;
    ScPathInfo result;clear(&result,sizeof(result));
    result.size=sizeof(result);result.version=1;result.kind=raw[1];result.length=raw[2];
    result.uid=(i32)raw[3];result.gid=(i32)raw[4];result.rw=raw[5];result.flags=raw[6];result.generation=raw[7];
    return publish(pid,output,&result,sizeof(result));
}
