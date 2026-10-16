/* 工程搜索路径只读本人环境一次；unset保持全部旧搜索顺序。
 * 255B是现ENV真实容量，不放大SCX/SCAPI，也不静默截断目录。 */
static char project_include_dirs[256];
static int project_include_bytes;
static void project_include_init(void)
{
    int n=sc_user(4,"SCCC_INCLUDE",project_include_dirs,sizeof(project_include_dirs));
    if(n==-2){
        project_include_bytes=0;
        return;
    }
    if(n<=0 || n>255)fatal("SCCC_INCLUDE must be nonempty within 255 bytes");
    project_include_bytes=n;
    int start=0;
    for(int at=0;at<=n;at++){
        int c=(u8)project_include_dirs[at];
        if(at<n && c!=';'){
            if(c<=32 || c=='\\' || c==':')fatal("SCCC_INCLUDE contains whitespace or an invalid path byte");
            continue;
        }
        int size=at-start;
        if(size<1 || size>63 || project_include_dirs[start]!='/')fatal("SCCC_INCLUDE needs absolute directories within 63 bytes and no empty items");
        char directory[64];
        for(int k=0;k<size;k++)directory[k]=project_include_dirs[start+k];
        directory[size]=0;
        u32 info[2];
        if(sc_stat(directory,info)<0 || info[0]!=2)fatal("SCCC_INCLUDE item is not an accessible directory");
        start=at+1;
    }
}
static int project_include_find(const char *name,char candidate[64])
{
    int at=0;
    while(at<project_include_bytes){
        int start=at;
        while(at<project_include_bytes && project_include_dirs[at]!=';')at++;
        int n=at-start;
        if(at<project_include_bytes)at++;
        /* ENV是绝对显示名，READ用rootless规范身份；仅去一个根斜杠，
         * 不再RESOLVE叠cwd。末尾/和根目录都按同一join处理。 */
        int used=n-1;
        for(int k=0;k<used;k++)candidate[k]=project_include_dirs[start+1+k];
        if(used && candidate[used-1]!='/')candidate[used++]='/';
        int bytes=length(name);
        if(bytes>63-used)fatal("project include path exceeds 63 bytes");
        for(int k=0;k<=bytes;k++)candidate[used+k]=name[k];
        if(load_source(candidate)>=0)return 1;
    }
    return 0;
}
