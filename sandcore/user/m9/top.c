#include "../SCTEXT.H"
static u32 old_cpu[200],cpu[200],processes[520],memory[48];
static int old_valid;
static u32 fraction(u32 n,u32 d){return d?n/d*100+(n%d)*100/d:0;}
static int draw(int batch,int terminal)
{
    if(sc_cpu2(cpu)<0 || sc_processes2(processes,520)<0 || sc_monitor(memory)<0)return -1;
    if(!batch){int r;while((r=sc_terminal_clear(terminal))==-6)sc_yield();if(r<0)return -1;}
    cli_text(1,"SandCore tasks   uptime ");text_unsigned(1,(u32)sc_tick()/100);cli_text(1,"s\nMemory KiB: used ");text_unsigned(1,memory[3]/1024);cli_text(1," / ");text_unsigned(1,memory[2]/1024);cli_text(1,"   free ");text_unsigned(1,memory[4]/1024);cli_text(1,"\nCPU PIT samples: ");
    u32 total=old_valid?cpu[2]-old_cpu[2]:0;if(total){cli_text(1,"user ");text_unsigned(1,fraction(cpu[5]-old_cpu[5],total));cli_text(1,"%  kernel ");text_unsigned(1,fraction(cpu[4]-old_cpu[4],total));cli_text(1,"%  idle ");text_unsigned(1,fraction(cpu[3]-old_cpu[3],total));cli_text(1,"%\n");}
    else cli_text(1,"waiting for interval\n");cli_text(1,"PID  UID  GID  STATE  CPU%  SAMPLES  NAME\n");
    int order[32],count=0;u32 usage[32]={0};for(u32 i=1;i<processes[1];i++){u32 *row=processes+8+i*16,*sample=cpu+8+i*6,*old=old_cpu+8+i*6;
        if(!row[1] || row[1]==2)continue;order[count++]=(int)i;if(total && sample[5] && row[2]==sample[2] && old[2]==sample[2])usage[i]=sample[3]-old[3];}
    for(int i=1;i<count;i++){int v=order[i],j=i;while(j && usage[order[j-1]]<usage[v]){order[j]=order[j-1];j--;}order[j]=v;}
    u32 info[8];int rows=count;if(!batch && sc_terminal_info2(terminal,info)>=0 && info[3]>5 && rows>(int)info[3]-5)rows=(int)info[3]-5;
    for(int k=0;k<rows;k++){int i=order[k];u32 *row=processes+8+i*16,*sample=cpu+8+i*6;cli_number(1,i);cli_text(1,"  ");cli_number(1,(int)row[3]);cli_text(1,"  ");cli_number(1,(int)row[4]);
        cli_text(1,"  ");cli_text(1,row[1]==3?"pause":"run");cli_text(1,"  ");if(sample[5] && sample[2]==row[2]){text_unsigned(1,fraction(usage[i],total));cli_text(1,"  ");text_unsigned(1,sample[3]);}else cli_text(1,"-  -");
        cli_text(1,"  ");cli_text(1,((char *)(row+8))[0]?(char *)(row+8):"(private)");cli_text(1,"\n");}
    for(int i=0;i<200;i++)old_cpu[i]=cpu[i];old_valid=1;return 0;
}
int main(void)
{
    if(cli_parse()<0)return 2;int batch=0,count=-1;u32 interval=100;
    for(int i=0;i<cli_argc;i++){const char *p=cli_argv[i];if(equal(p,"-b"))batch=1;else if(equal(p,"-n")){if(++i==cli_argc || cli_integer(cli_argv[i],&count)<0 || count<1)return 2;}
        else if(equal(p,"-d")){u32 seconds;if(++i==cli_argc || text_number(cli_argv[i],&seconds)<0 || !seconds || seconds>86400)return 2;interval=seconds*100;}else return 2;}
    u32 info[8];int terminal=-1;if(!batch){if(sc_terminal_info2(1,info)<0 || (info[1]!=4 && info[1]!=5))return 2;terminal=sc_stream_open("",8,0);if(terminal<0)return 1;}
    if(batch && count<0)count=1;int result=0,frames=0;for(;;){u32 start=(u32)sc_tick();if(draw(batch,terminal)<0){result=1;break;}if(++frames==count)break;
        while((u32)sc_tick()-start<interval){if(terminal>=0){u8 c;int n=sc_stream_read(terminal,&c,1);if(n==0 || (n>0 && (c=='q' || c=='Q' || c==27)))goto done;if(n<0 && n!=-6){result=1;goto done;}}sc_yield();}}
done:
    if(terminal>=0)sc_stream_close(terminal,0);return result;
}
