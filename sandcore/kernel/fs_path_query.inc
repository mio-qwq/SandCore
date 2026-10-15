/* 在fs.c正文末尾包含；只查当前已提交目录，不把候选/失败写盘当成功。 */
int fs_path_query(int owner,const char *path,u32 out[8])
{
    if(version!=5 || !dir_sectors)return FS_N_NOT_SUPPORTED;
    if(fs_faulted)return FS_N_IO;
    char name[64];int length=fs_normalize(path,name);
    if(length<0)return FS_N_INVALID;
    /* 先核META祖先，不能借精确不存在错误探查不可遍历目录。 */
    if(!fs_access(owner,name,FS_ACCESS_META))return FS_N_ACCESS;
    if(length && find(name)<0)return FS_N_NOT_FOUND;
    u32 info[8];
    if(fs_metadata(name,info))return FS_N_IO;
    for(u32 i=0;i<8;i++)out[i]=info[i];
    return 0;
}
