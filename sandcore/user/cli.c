/* =====================================================================
 * mio：M8独立纯命令行工具，共用实现按真实执行路径选择命令。
 *
 * 每个/BIN/*.SCX都是可替换的普通三环程序；没有WINOPEN，不侵占
 * 第七个窗口。共享源码/载荷减少磁盘与原生自编译时间，执行名称由
 * 内核记录，不相信用户把argv伪造为另一个命令。所有相对文件路径
 * 先RESOLVE，再调用保留根语义的FS接口；失败从不假报复制成功。
 * ===================================================================== */
#include "SCAPI.H"
static char arguments[128],program[65],resolved[64],second[64],buffer[4096];

static int error(const char *message,int result)
{
    char text[160],number[12];decimal(number,result);
    copy(text,message,160);append(text,": ",160);append(text,number,160);append(text,"\n",160);
    sc_puts(text);return 1;
}
static void number(u32 value)
{char text[12];decimal(text,(int)value);sc_puts(text);}
static int path(const char *text,char *out)
{return sc_resolve(text,out,64);}
static int file_args(char **p,int two)
{
    char *one=token(p);if(!*one || path(one,resolved)<0)return -1;
    if(two) {char *value=token(p);if(!*value || path(value,second)<0)return -1;}
    return **p?-1:0;
}
static int list(const char *arg)
{
    if(path(arg,resolved)<0)return error("ls: invalid path",-1);
    u32 info[2];if(sc_stat(resolved,info)<0 || info[0]!=2)return error("ls: directory unavailable",-2);
    /* 每行由FSDIR提供直接子项，不再把整个盘平铺成“当前目录”。
     * 当前512项的最坏文本超出4KB，用私有高端堆完整读取，防止
     * 无声截断长目录；列表缓冲属于应用，退出自动回收。 */
    char *listing=sc_alloc(65536);if(!listing)return error("ls: memory",-4);
    int n=sc_dir(resolved,listing,65536);if(n<0){sc_free(listing);return error("ls: read",n);}
    char *p=listing;
    while(*p) {
        char kind=*p++;if(*p==' ')p++;
        char *name=p;while(*p && *p!=' ')p++;
        if(!*p)break;
        *p++=0;
        char *size=p;while(*p && *p!='\n')p++;
        if(*p)*p++=0;
        char row[120];copy(row,kind=='D'?"[DIR]  ":"       ",120);
        append(row,name,120);
        if(kind=='D')append(row,"/",120);else {append(row,"  ",120);append(row,size,120);append(row," B",120);}
        append(row,"\n",120);sc_puts(row);
    }
    sc_free(listing);return 0;
}
static int cat(const char *arg)
{
    if(path(arg,resolved)<0)return error("cat: invalid path",-1);
    u32 info[2];if(sc_stat(resolved,info)<0 || info[0]!=1)return error("cat: file unavailable",-2);
    /* 流式读取保留UTF-8跨块尾字节；一次最多4092正文，给完整4字节
     * 标量和终止符留空间。二进制控制/NUL明确诊断，不能将黑盒字节
     * 喂给终端当控制命令。失败返回码由Shell等待后显示。 */
    u32 offset=0;int carry=0;
    while(offset<info[1] || carry) {
        int n=sc_read_at(resolved,buffer+carry,4092-carry,offset);
        if(n<0)return error("cat: read",n);
        offset+=(u32)n;n+=carry;int end=n;
        if(offset<info[1]) {
            int at=n-1;while(at>=0 && ((u8)buffer[at]&0xC0)==0x80)at--;
            if(at>=0) {
                int lead=(u8)buffer[at],need=lead<128?1:lead<224?2:lead<240?3:4;
                if(n-at<need)end=at;
            }
        }
        for(int i=0;i<end;i++)if(!buffer[i] || ((u8)buffer[i]<32 && buffer[i]!='\n' && buffer[i]!='\r' && buffer[i]!='\t'))return error("cat: binary/control data",-2);
        char tail[4];carry=n-end;for(int i=0;i<carry;i++)tail[i]=buffer[end+i];
        buffer[end]=0;
        int result=sc_call(1,(int)buffer,0,0,0,0,0);if(result<0)return error("cat: invalid text",result);
        for(int i=0;i<carry;i++)buffer[i]=tail[i];
    }
    sc_puts("\n");return 0;
}
static void percent(u32 numerator,u32 denominator)
{
    if(!denominator){sc_puts("sampling");return;}
    number(numerator*100/denominator);sc_puts("%");
}
static int system_command(const char *name)
{
    /* mio：普通三环工具使用公开快照。当前设备LBA0就是整盘卷，
     * 不把路径子目录伪装成磁盘分区；FDC信息明确为启动约定。 */
    if(equal(name,"lsblk") || equal(name,"partitions") || equal(name,"df")) {
        u32 disk[SC_STORAGE_WORDS];if(sc_storage(disk)<0)return error("storage query",-1);
        if(equal(name,"lsblk")) {
            sc_puts("DEVICE    KIND       SIZE KiB     LAYOUT\nFLOPPY0   boot       ");number(disk[4]/2);
            sc_puts("         boot contract / ATA cannot enumerate FDC\n");
            if(disk[1]){sc_puts("IDE0      ATA LBA28  ");number(disk[3]/2);sc_puts("        ");sc_puts(disk[6]?"whole-device SandFS\n":"volume invalid / unformatted\n");}
            return 0;
        }
        if(!disk[6])return error("no valid SandFS volume",-2);
        if(equal(name,"partitions")) {
            sc_puts("IDE0: no MBR/GPT partition table\nSandFS is a whole-device volume at LBA 0\nVersion ");number(disk[7]);
            sc_puts("; sectors ");number(disk[8]);sc_puts("; directory ");number(disk[14]);sc_puts(" sectors\n");return 0;
        }
        sc_puts("VOLUME   TOTAL KiB  APPEND FREE KiB  LIVE KiB  ENTRIES\nIDE0:/   ");number(disk[8]/2);sc_puts("      ");number(disk[11]/2);
        sc_puts("             ");number(disk[15]/2);sc_puts("      ");number(disk[13]);sc_puts("/");number(disk[12]);
        sc_puts("\nFree means append capacity; deleted data holes are not reclaimed.\n");return 0;
    }
    if(equal(name,"ps")) {
        u32 page[SC_PROCESS_PAGE_WORDS],cursor=0xFFFFFFFFu;
        const char *states[]={"empty","runnable","zombie","paused"};
        sc_puts("PID  STATE     NAME         RUN SAMPLES  DISPATCHES  GENERATION\n");
        do{
            if(sc_process_page(page,SC_PROCESS_PAGE_WORDS,cursor)<0)return error("task query",-1);
            for(u32 i=0;i<page[3];i++){
                u32 *row=page+16+i*16;char label[13];
                for(int j=0;j<12;j++)label[j]=((char *)(row+8))[j];label[12]=0;
                number(row[0]);sc_puts("    ");sc_puts(row[1]<4?states[row[1]]:"unknown");sc_puts("  ");sc_puts(row[0]?label:"kernel");sc_puts("   ");
                number(row[5]);sc_puts("          ");number(row[6]);sc_puts("          ");number(row[2]);sc_puts("\n");
            }
            cursor=page[4];
        }while(cursor!=0xFFFFFFFFu);
        sc_puts("Samples are cumulative guest PIT states, not exact cycles.\n");return 0;
    }
    if(equal(name,"cpu")) {
        u32 before[SC_CPU_WORDS],after[SC_CPU_WORDS];if(sc_cpu(before)<0)return 1;
        u32 start=(u32)sc_tick();while((u32)sc_tick()-start<100)sc_yield();
        if(sc_cpu(after)<0)return 1;
        u32 total=after[3]-before[3];sc_puts("Guest CPU / 100Hz PIT / one-second observation\nBusy ");
        percent(total-(after[4]-before[4]),total);sc_puts("; idle ");percent(after[4]-before[4],total);
        sc_puts("; kernel ");percent(after[5]-before[5],total);sc_puts("; user ");percent(after[6]-before[6],total);
        sc_puts("; graphics (inside kernel) ");percent(after[7]-before[7],total);
        sc_puts("\nIF=0 intervals are not sampled; this is not host QEMU CPU.\n");return 0;
    }
    return -1;
}
int main(void)
{
    if(sc_user(3,0,program,65)<0)return 1;
    char *name=program;
    for(int i=0;program[i];i++)if(program[i]=='/')name=program+i+1;
    for(int i=0;name[i];i++) {
        if(name[i]=='.'){name[i]=0;break;}
        if(name[i]>='A' && name[i]<='Z')name[i]=name[i]-'A'+'a';
    }
    sc_args(arguments,128);char *p=arguments;
    int system=system_command(name);if(system>=0)return system;
    if(equal(name,"echo")){sc_puts(arguments);sc_puts("\n");return 0;}
    if(equal(name,"pwd")){sc_user(2,0,program,65);sc_puts(program);sc_puts("\n");return 0;}
    if(equal(name,"whoami")){sc_user(0,0,program,65);sc_puts(program);sc_puts("\n");return 0;}
    if(equal(name,"ls"))return list(arguments);
    if(equal(name,"cat")){if(file_args(&p,0)<0)return error("usage: cat FILE",-1);return cat(arguments);}
    if(equal(name,"help")) {
        sc_puts("Shell: cd [DIR], clear, run ROOT/PATH.SCX, exit\nCommands in /BIN:\nls [DIR], pwd, cat FILE, echo TEXT, whoami\nstat FILE, cp FROM TO, mkdir DIR, rm PATH, mv FROM TO\nmem, uptime, env [NAME], help\nlsblk, partitions, df, ps, cpu\nUp / Down: command history.  Escape: close.\n");return 0;
    }
    if(equal(name,"env")) {
        char value[256];char *key=*arguments?arguments:"PATH";
        int result=sc_user(4,key,value,256);if(result<0)return error("env: missing variable",result);
        sc_puts(key);sc_puts("=");sc_puts(value);sc_puts("\n");return 0;
    }
    if(equal(name,"mem") || equal(name,"uptime")) {
        u32 monitor[48];if(sc_monitor(monitor)<0)return 1;
        if(equal(name,"uptime")){number(monitor[1]/100);sc_puts(" seconds\n");}
        else {sc_puts("Managed ");number(monitor[2]/1024);sc_puts(" KiB; used ");number(monitor[3]/1024);sc_puts(" KiB; free ");number(monitor[4]/1024);sc_puts(" KiB\n");}
        return 0;
    }
    if(equal(name,"mkdir") || equal(name,"rm") || equal(name,"stat")) {
        if(file_args(&p,0)<0)return error("one path required",-1);
        int result;
        if(equal(name,"stat")) {
            u32 info[2];result=sc_stat(resolved,info);
            if(!result){sc_puts(info[0]==2?"Directory: ":"File: ");sc_puts(resolved);sc_puts("  ");number(info[1]);sc_puts(" B\n");}
        }else result=equal(name,"mkdir")?sc_mkdir(resolved):sc_remove(resolved);
        return result<0?error("file operation failed",result):0;
    }
    if(equal(name,"cp") || equal(name,"mv")) {
        if(file_args(&p,1)<0)return error("two paths required",-1);
        if(equal(name,"mv")){int r=sc_rename(resolved,second);return r<0?error("mv failed",r):0;}
        u32 info[2];if(sc_stat(resolved,info)<0 || info[0]!=1)return error("cp: source",-2);
        /* 当前FSWRITE为整文件提交，复制不能在目标不存在时逐段追加。
         * 在高端私有堆读完再提交，完整读取失败不碰目标。权限仍由
         * 内核FSWRITE统一检查，不能借本工具绕过SYS/CORE保护。 */
        void *data=sc_alloc(info[1]?info[1]:1);if(!data)return error("cp: memory",-4);
        int read=sc_read(resolved,data,(int)info[1]);
        if(read!=(int)info[1]){sc_free(data);return error("cp: incomplete read",read);}
        int written=sc_write(second,data,(int)info[1]);sc_free(data);
        return written==(int)info[1]?0:error("cp: write",written);
    }
    return error("unknown CLI name",-1);
}
