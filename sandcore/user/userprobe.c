/* mio：用户空间ABI的普通三环验收探针。断言写入本任务变量供只读
 * QEMU诊断；不能用“窗口显示PASS”替代非法指针/有效快照/父作业
 * 结果的实际检查。全部配置只写自动测试盘，默认开发盘不受影响。 */
#include "SCAPI.H"
volatile u32 user_probe[16];
static void check(int condition,int where)
{if(!condition){user_probe[1]++;user_probe[2]=(u32)where;}}
static int save(const char *path,const char *text)
{return sc_write(path,text,length(text))==length(text);}
int main(void)
{
    char args[128];sc_args(args,128);
    if(equal(args,"linger")){
        sc_puts("Attached CLI waits / closing parent must cancel\n");
        u32 begin=(u32)sc_tick();while((u32)sc_tick()-begin<10000)sc_yield();return 0;
    }
    if(equal(args,"rogue")) {
        int result=sc_call(1,(int)"UNAUTHORIZED OUTPUT",0,0,0,0,0);
        save("HOME/ROGUE",result==-1?"DENIED":"FAIL");return 7;
    }
    int w=sc_open_rgb("User ABI",480,240);if(w<0)return 1;
    check(sc_terminal(w,0)==0,1);
    char text[256],guard[4]="XYZ",before[65];
    check(sc_user(0,0,text,256)>0 && equal(text,"mio"),2);
    check(sc_user(4,"PATH",text,256)==10 && equal(text,"/BIN:/APPS"),3);
    check(sc_user(2,0,before,65)>0,4);
    check(sc_user(0,0,guard,1)==-1 && equal(guard,"XYZ"),5);
    check(sc_call(0x70,0,0,0x10000,64,0,0)==-1,6);
    check(sc_call(0x70,4,0x10000,(int)text,256,0,0)==-1,7);
    check(sc_user(4,"MISSING",text,256)==-2,8);
    check(sc_call(0x76,w+100,0,0,0,0,0)==-1,9);
    check(sc_call(0x74,(int)"echo bad",w+100,0,0,0,0)==-1,10);
    check(sc_chdir("/HOME")==0,11);
    check(sc_resolve("../BIN/./LS.SCX",text,64)==10 && equal(text,"BIN/LS.SCX"),12);
    check(sc_resolve("../../BIN",text,64)==-1,13);
    check(sc_chdir("/BIN/LS.SCX")==-2,14);
    check(sc_user(2,0,text,256)==5 && equal(text,"/HOME"),15);
    check(save("HOME/UCFG","SUSER1MIO\nusername=visitor\nhome=/HOME\n"),16);
    check(sc_user_config(0,"HOME/UCFG",0)==0,17);
    check(sc_user(0,0,text,256)==3 && equal(text,"mio"),18);
    check(sc_user_config(0,"HOME/UCFG",1)==0,19);
    check(sc_user(0,0,text,256)==7 && equal(text,"visitor"),20);
    const char *bad[]={"SUSER1MIO\nusername=x@y\nhome=/HOME\n", "SUSER1MIO\nusername=a\nusername=b\nhome=/HOME\n", "SUSER1MIO\nusername=a\nhome=HOME\n", "SUSER1MIO\nusername=a\nhome=/HOME\nunknown=1\n"};
    for(int i=0;i<4;i++) {
        check(save("HOME/UCFG",bad[i]),21+i);
        check(sc_user_config(0,"HOME/UCFG",1)==-1,25+i);
        check(sc_user(0,0,text,256)==7 && equal(text,"visitor"),29+i);
    }
    check(save("HOME/UCFG","SUSER1MIO\nusername=mio\nhome=/HOME\n"),33);
    check(sc_user_config(0,"HOME/UCFG",1)==0,34);
    check(save("HOME/ECFG","SENV1MIO\nPATH=/BIN:/APPS\nCOLOR=warm\n"),35);
    check(sc_user_config(1,"HOME/ECFG",1)==0,36);
    check(sc_user(4,"COLOR",text,256)==4 && equal(text,"warm"),37);
    const char *envbad[]={"SENV1MIO\nPATH=/BIN::/APPS\n", "SENV1MIO\nPATH=BIN:/APPS\n", "SENV1MIO\nPATH=/BIN:/APPS:\n", "SENV1MIO\nPATH=/BIN\nPATH=/APPS\n", "SENV1MIO\nPATH=/BIN\n1A=x\n"};
    for(int i=0;i<5;i++) {
        check(save("HOME/ECFG",envbad[i]),38+i);
        check(sc_user_config(1,"HOME/ECFG",1)==-1,43+i);
        check(sc_user(4,"COLOR",text,256)==4 && equal(text,"warm"),48+i);
    }
    int ticket=sc_cli("echo ABI output",w);check(ticket>0,53);
    if(ticket>0) {
        int result;do{result=sc_job(ticket);sc_yield();}while(result==0x40000000 || result==0x40000001);
        check(result==0,54);check(sc_job(ticket+1)==-1,55);
        /* 下一次EXEC复用任务槽不应覆盖旧CLI票据的完成结果。 */
        int other=sc_exec("HOME/UP.SCX rogue");check(other>0,56);
        if(other>0)while(sc_status(other)==0x40000000)sc_yield();
        check(sc_job(ticket)==0,57);
        check(sc_read("HOME/ROGUE",text,6)==6,59);text[6]=0;check(equal(text,"DENIED"),60);
    }
    check(sc_chdir(before)==0,58);
    check(save("HOME/UCFG","SUSER1MIO\nusername=visitor\nhome=/HOME\n"),61);
    check(sc_user_config(0,"HOME/UCFG",1)==0,62);
    sc_puts("User context / configuration / job ABI\n");user_probe[0]=1;
    while(sc_key()!=27)sc_yield();return user_probe[1]?1:0;
}
