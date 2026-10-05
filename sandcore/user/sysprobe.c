/* =====================================================================
 * mio：CPU/存储/旧ABI兼容的普通三环验收程序，不修改内核状态。
 * QEMU只读sys_probe并输入实际键鼠；分类来自公开API，容量测试通过
 * 正常MKDIR/RENAME/REMOVE执行，512项证明不能靠宿主造目录冒充。
 * 满盘在stage=4等待，验证器冷启动同一盘后运行cleanup检查持久化。
 * ===================================================================== */
#include "SCAPI.H"
volatile u32 sys_probe[20];
static u32 start_cpu[SC_CPU_WORDS],end_cpu[SC_CPU_WORDS],disk[SC_STORAGE_WORDS];
static void check(int condition,int where)
{if(!condition){sys_probe[1]++;sys_probe[2]=(u32)where;}}
static void cpu_check(u32 *out)
{
    check(out[0]==1 && out[1]==100 && out[8]==8 && out[9]==1,1);
    check(out[3]==out[4]+out[5]+out[6] && out[7]<=out[5],2);
}
static void wait_ticks(u32 ticks,int burn)
{
    u32 at=(u32)sc_tick();volatile u32 work=1;
    while((u32)sc_tick()-at<ticks){
        if(burn)for(int i=0;i<2048;i++)work=work*1664525u+1013904223u;
        else sc_yield();
    }
    sys_probe[19]=work;
}
static void item(char *out,int index)
{
    copy(out,"HOME/CAP/F000",64);
    out[10]=(char)('0'+index/100);out[11]=(char)('0'+index/10%10);out[12]=(char)('0'+index%10);
}
static void cleanup(void)
{
    sc_storage(disk);check(disk[13]==disk[12],30);sys_probe[6]=disk[12];sys_probe[7]=disk[13];
    /* 满盘也必须允许整组改名：并不新建条目；超长失败不能占临时页。 */
    u32 memory[48];sc_monitor(memory);u32 used=memory[3];
    check(sc_rename("HOME/CAP","HOME/ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZABCDE")==-1,31);
    sc_monitor(memory);check(memory[3]==used,32);
    check(sc_rename("HOME/CAP","HOME/MOVED")==0,33);
    sc_monitor(memory);check(memory[3]==used,34);
    char *listing=sc_alloc(65536);check(listing!=0,35);
    int count=0;
    if(listing){
        int n=sc_dir("HOME/MOVED",listing,65536);check(n>=0,36);
        for(int i=0;i<n;i++)if(listing[i]=='\n')count++;
        sc_free(listing);
    }
    sys_probe[8]=(u32)count;
    check(count>0 && count<512,37);
    for(int i=0;i<count;i++){
        char name[64];item(name,i);char path[64];copy(path,"HOME/MOVED/",64);append(path,name+9,64);
        check(sc_remove(path)==0,38);
    }
    check(sc_remove("HOME/MOVED")==0,39);
    sc_storage(disk);check(disk[13]+(u32)count+1==disk[12],40);
    sys_probe[0]=5;
}
int main(void)
{
    char args[128];sc_args(args,128);
    int win=sc_open_rgb("CPU / STORAGE / ABI",640,360);if(win<0)return 1;
    sc_fill_rgb(win,0,0,1920,1080,SC_RGB_PAPER);
    sc_text_rgb(win,20,18,"Guest CPU / storage / old ABI proof",SC_RGB_INK);
    if(equal(args,"cleanup")){
        cleanup();sc_text_rgb(win,20,52,"Cold boot: full directory checked and cleaned",SC_RGB_ACCENT);
        while(sc_key()!=27)sc_yield();return sys_probe[1]?1:0;
    }
    check(sc_cpu(start_cpu)==0 && sc_storage(disk)==0,3);cpu_check(start_cpu);
    check(sc_call(0x79,0x10000,0,0,0,0,0)==-1,4);
    check(sc_call(0x7A,0x10000,0,0,0,0,0)==-1,5);
    u32 old[50];old[48]=0x1234ABCDu;old[49]=0x89ABCDEFu;
    check(sc_monitor(old)==0 && old[48]==0x1234ABCDu && old[49]==0x89ABCDEFu,6);
    u32 *page=sc_alloc(4096);check(page!=0,7);
    if(page){
        for(int i=0;i<32;i++)page[992+i]=0xA5A5A5A5u;
        check(sc_cpu(page+992)==-1,8);
        for(int i=0;i<32;i++)check(page[992+i]==0xA5A5A5A5u,9);
        check(sc_storage(page+1008)==-1,10);
        for(int i=0;i<32;i++)check(page[992+i]==0xA5A5A5A5u,11);
        sc_free(page);
    }
    check(disk[0]==1 && disk[1]==1 && disk[2]==512 && disk[3]==131072,12);
    check(disk[6]==1 && disk[7]==4 && disk[8]<=disk[3] && disk[18]==1,13);
    check(disk[9]==1+disk[14] && disk[10]+disk[11]==disk[8],14);
    sc_cpu(start_cpu);wait_ticks(150,0);sc_cpu(end_cpu);cpu_check(end_cpu);
    u32 total=end_cpu[3]-start_cpu[3];check(total>100,15);
    sys_probe[3]=total?(end_cpu[4]-start_cpu[4])*100/total:0;check(sys_probe[3]>40,16);
    sc_cpu(start_cpu);wait_ticks(200,1);sc_cpu(end_cpu);cpu_check(end_cpu);
    total=end_cpu[3]-start_cpu[3];sys_probe[4]=total?(end_cpu[6]-start_cpu[6])*100/total:0;
    check(total>100 && sys_probe[4]>15,17);
    sc_text_rgb(win,20,52,"Press A: old GETKEY ignores EBX and consumes",SC_RGB_ACCENT);
    sys_probe[0]=1;
    while(sc_key_peek()<0)sc_yield();
    check(sc_key_peek()=='a' && sc_key_peek()=='a',18);
    check(sc_call(0x14,1,0,0,0,0,0)=='a' && sc_key_peek()==-1,19);
    int pointer[6];sc_pointer(win,pointer);
    sc_text_rgb(win,20,86,"Click client: old POINTER ignores EDX and consumes",SC_RGB_ACCENT);
    sys_probe[0]=2;
    do{sc_pointer_peek(win,pointer);sc_yield();}while(!pointer[3] && !pointer[4]);
    int edges=pointer[3]|pointer[4];check(edges!=0,20);
    sc_pointer_peek(win,pointer);check((pointer[3]|pointer[4])!=0,21);
    check(sc_call(0x22,win,(int)pointer,1,0,0,0)==0,22);
    sc_pointer_peek(win,pointer);check(!pointer[3] && !pointer[4],23);
    check(sc_write("SYS/CORE/ABI.TEST",(void *)"x",1)==-5,24);
    check(sc_mkdir("HOME/CAP")==0,25);sc_storage(disk);
    u32 capacity=disk[12];int added=0;
    while(disk[13]<capacity){
        char name[64];item(name,added);check(sc_mkdir(name)==0,26);added++;
        sc_storage(disk);sys_probe[0]=3;sys_probe[9]=(u32)added;
        if(sys_probe[1])break;
    }
    check(sc_mkdir("HOME/CAP/OVERFLOW")==-1,27);
    sc_storage(disk);check(disk[13]==capacity,28);
    sys_probe[6]=capacity;sys_probe[7]=disk[13];sys_probe[0]=4;
    sc_text_rgb(win,20,122,"Directory full / boundary rejected / ready for cold boot",SC_RGB_ACCENT);
    while(sc_key()!=27)sc_yield();return sys_probe[1]?1:0;
}
