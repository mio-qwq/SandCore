#include "SCDBG.H"
#include "SCTEXT.H"
typedef struct {
    u32 job,generation,frame[22],event,address,breaks[16];int pid,input,terminal,active,paused,context,break_count,result;
    char command[1024],map[16385],pending[4096];u32 pending_at,pending_size;int input_eof;
} debug_state;
static void debug_flush(debug_state *d)
{
    /* 已暂停目标不能消耗stdin。写满管道时只保留有界待发队列，
     * 提示符继续接收cont/pause，不能堵在等待暂停目标读输入的循环。 */
    if(d->input<0)return;
    if(d->pending_at<d->pending_size){int r=sc_stream_write(d->input,d->pending+d->pending_at,d->pending_size-d->pending_at);
        if(r==-6)return;if(r<=0){cli_error("scdbg: target input",r?r:-1);sc_stream_close(d->input,0);d->input=-1;return;}d->pending_at+=(u32)r;}
    if(d->pending_at==d->pending_size){d->pending_at=d->pending_size=0;if(d->input_eof){sc_stream_close(d->input,0);d->input=-1;}}
}
static int debug_input(debug_state *d,const char *text)
{
    if(d->input<0 || d->input_eof)return -1;u32 used=d->pending_size-d->pending_at,bytes=(u32)length(text)+1;
    if(bytes>sizeof(d->pending)-used)return -6;
    for(u32 i=0;i<used;i++)d->pending[i]=d->pending[d->pending_at+i];
    for(u32 i=0;i<bytes-1;i++)d->pending[used+i]=text[i];d->pending[used+bytes-1]='\n';d->pending_at=0;d->pending_size=used+bytes;debug_flush(d);return 0;
}
static int debug_hex(const char *p,u32 *out)
{if(p[0]=='0' && (p[1]=='x'||p[1]=='X'))p+=2;u32 value=0,used=0;while(*p){u8 c=(u8)*p++;int d=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;
    if(d<0 || used==8)return -1;used++;value=(value<<4)|(u32)d;}if(!used)return -1;*out=value;return 0;}
static int debug_state_read(debug_state *d,int announce)
{
    if(!d->active)return 0;u32 state[8];int r=sc_wait2(d->job,state);
    if(r<0 || (r>0 && ((int)state[3]!=d->pid || state[4]!=d->generation))){d->active=0;d->pid=-1;d->context=0;return -1;}
    if(!r){d->active=0;d->pid=-1;d->context=0;d->result=(int)state[2];cli_text(1,"Target exited ");cli_number(1,d->result);cli_text(1,"\n");return 0;}
    int changed=d->paused!=(r==2);d->paused=r==2;
    if(d->paused){r=sc_debug(d->pid,0,0,d->frame,sizeof(d->frame));if(r<0){d->context=0;return r;}d->context=1;
        if(d->event!=d->frame[21]){changed=1;d->event=d->frame[21];d->address=d->frame[14];}}
    else d->context=0;
    if(announce && changed){cli_text(1,d->paused?"Paused EIP=":"Running");if(d->paused)text_hex(1,d->frame[14],8);cli_text(1,"\n");}
    return d->paused?2:1;
}
static void debug_help(void)
{
    cli_text(1,"SandDebug CLI\nregs | step [N] | cont | pause | break HEX | clear HEX | breaks\nmem [HEX] [LEN] | stack [N] | code [HEX] [LEN] | where | status\ninput TEXT | eof | restart | quit\nAddresses are hexadecimal; lengths/counts are decimal. Target starts paused.\n");
}
static void debug_registers(debug_state *d)
{
    if(!d->context){cli_text(2,"Target must be paused\n");return;}
    const char *names[8]={"EDI","ESI","EBP","ESP0","EBX","EDX","ECX","EAX"};
    for(int i=0;i<8;i++){cli_text(1,names[i]);cli_text(1,"=");text_hex(1,d->frame[4+i],8);cli_text(1,i==3||i==7?"\n":" ");}
    cli_text(1,"EIP=");text_hex(1,d->frame[14],8);cli_text(1," ESP=");text_hex(1,d->frame[17],8);cli_text(1," EFLAGS=");text_hex(1,d->frame[16],8);
    cli_text(1," VECTOR=");text_unsigned(1,d->frame[12]);cli_text(1," CR2=");text_hex(1,d->frame[20],8);cli_text(1," EVENT=");text_unsigned(1,d->frame[21]);cli_text(1,"\n");
}
static void debug_symbol(debug_state *d,u32 address)
{
    char *p=d->map,*best=0;u32 nearest=0,best_size=0;
    while(*p){char *line=p;while(*p && *p!='\n')p++;u32 size=(u32)(p-line);if(*p)p++;if(size<10 || line[8]!=' ')continue;
        char value[9];for(int i=0;i<8;i++)value[i]=line[i];value[8]=0;u32 at;if(debug_hex(value,&at)<0 || at>address || (best && at<nearest))continue;
        nearest=at;best=line+9;best_size=size-9;}
    if(best){cli_write(1,best,best_size);cli_text(1," +0x");text_hex(1,address-nearest,8);cli_text(1,"\n");}else cli_text(1,"No symbol\n");
}
static int debug_memory(debug_state *d,u32 address,u32 size)
{
    if(!size || size>256 || address>0xFFFFFFFFu-(size-1))return -1;u8 bytes[256];int r=sc_debug(d->pid,3,address,bytes,size);if(r<0)return r;
    for(u32 at=0;at<size;at+=16){text_hex(1,address+at,8);cli_text(1,"  ");u32 count=size-at;if(count>16)count=16;
        for(u32 i=0;i<16;i++){if(i<count)text_hex(1,bytes[at+i],2);else cli_text(1,"  ");cli_text(1," ");}cli_text(1," ");
        char text[17];for(u32 i=0;i<count;i++)text[i]=bytes[at+i]>=32 && bytes[at+i]<127?(char)bytes[at+i]:'.';text[count]=0;cli_text(1,text);cli_text(1,"\n");}
    d->address=address+size;return 0;
}
static u32 debug_word(const u8 *p){return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24);}
static int debug_modrm(const u8 *p,u32 count)
{
    if(!count)return 0;int mode=p[0]>>6,rm=p[0]&7;u32 n=1;
    if(mode!=3 && rm==4){if(n==count)return 0;int sib=p[n++];if(!mode && (sib&7)==5)n+=4;}
    if(!mode && rm==5)n+=4;if(mode==1)n++;if(mode==2)n+=4;return n<=count?(int)n:0;
}
static u32 debug_instruction(const u8 *p,u32 count,u32 address)
{
    /* 有明确长度合同的自写i386子集；未实现的SSE/前缀始终只显示db，
     * 不错吞后续字节。CPU的TF单步与此辅助视图完全独立。 */
    u32 n=1;u8 op=p[0];const char *name=0;int relative=0;
    if(op>=0x50 && op<=0x5F)name=op<0x58?"push reg":"pop reg";
    else if(op>=0xB8 && op<=0xBF){name="mov reg,imm32";n=5;}
    else if(op==0xE8 || op==0xE9){name=op==0xE8?"call":"jmp";n=5;relative=1;}
    else if(op==0xEB){name="jmp";n=2;relative=2;}
    else if(op>=0x70 && op<=0x7F){name="jcc";n=2;relative=2;}
    else if(op==0xC3)name="ret";else if(op==0xC9)name="leave";else if(op==0x90)name="nop";else if(op==0xCC)name="int3";else if(op==0x99)name="cdq";
    else if(op==0xCD){name="int";n=2;}else if(op==0x68 || op==0x05 || op==0x3D){name=op==0x68?"push imm32":op==0x05?"add eax,imm32":"cmp eax,imm32";n=5;}
    else if(op==0x0F && count>=2){if(p[1]>=0x80 && p[1]<=0x8F){name="jcc";n=6;relative=3;}
        else if(p[1]==0x0B){name="ud2";n=2;}else if(p[1]==0xAF || p[1]==0xBE || p[1]==0xBF || p[1]==0xB6 || p[1]==0xB7 || (p[1]>=0x90 && p[1]<=0x9F)){
            int m=debug_modrm(p+2,count-2);if(m){n=2+(u32)m;name=p[1]==0xAF?"imul":p[1]>=0x90?"setcc":p[1]==0xBE||p[1]==0xBF?"movsx":"movzx";}}}
    else if(op==0x89||op==0x8B||op==0x8D||op==0x88||op==0x8A||op==0x81||op==0x83||op==0x69||op==0x85||op==0x31||op==0x01||op==0x29||op==0x21||op==0x09||op==0x39||op==0xF7||op==0xFF||op==0xD3){
        int m=debug_modrm(p+1,count-1);if(m){n=1+(u32)m;name=op==0x8D?"lea":op==0x89||op==0x8B||op==0x88||op==0x8A?"mov reg/mem":op==0x85?"test":op==0x31?"xor":op==0xD3?"shift cl":"arith reg/mem";
            if(op==0x81||op==0x69)n+=4;if(op==0x83)n++;
            if(op==0xF7){int group=(p[1]>>3)&7;const char *names[]={"test imm32",0,"not","neg","mul","imul","div","idiv"};name=names[group];if(!group)n+=4;}
            if(op==0xFF){const char *names[]={"inc","dec","call",0,"jmp",0,"push",0};name=names[(p[1]>>3)&7];}}}
    if(!name || n>count){name="db";n=1;relative=0;}
    text_hex(1,address,8);cli_text(1,"  ");for(u32 i=0;i<n;i++){text_hex(1,p[i],2);cli_text(1," ");}cli_text(1," ");cli_text(1,name);
    if(relative){u32 offset=relative==1?debug_word(p+1):relative==3?debug_word(p+2):(u32)(int)(signed char)p[1];cli_text(1," ");text_hex(1,address+n+offset,8);}cli_text(1,"\n");return n;
}
static int debug_code(debug_state *d,u32 address,u32 size)
{
    if(!size || size>256 || address>0xFFFFFFFFu-(size-1))return -1;u8 bytes[256];int r=sc_debug(d->pid,3,address,bytes,size);if(r<0)return r;
    for(u32 at=0;at<size;)at+=debug_instruction(bytes+at,size-at,address+at);d->address=address+size;return 0;
}
static int debug_launch(debug_state *d)
{
    int fds[2];if(sc_stream_pipe(fds)<0)return -4;int child[3]={fds[0],1,2};int job=sc_dbgspawn2(d->command,child);sc_stream_close(fds[0],0);
    if(job<=0){sc_stream_close(fds[1],0);return job?job:-1;}u32 info[8];int r=sc_wait2((u32)job,info);
    if(r!=2){sc_stream_close(fds[1],0);if(r>0)sc_kill2((int)info[3]);return -1;}
    d->job=(u32)job;d->pid=(int)info[3];d->generation=info[4];d->input=fds[1];d->active=1;d->paused=0;d->event=0;d->context=0;d->break_count=0;d->pending_at=d->pending_size=0;d->input_eof=0;
    return debug_state_read(d,1)<0?-1:0;
}
static void debug_stop(debug_state *d)
{
    if(d->input>=0){sc_stream_close(d->input,0);d->input=-1;}
    if(d->active && debug_state_read(d,0)>0){sc_debug(d->pid,6,0,0,0);u32 info[8];sc_wait2(d->job,info);}
    d->active=0;d->pid=-1;d->context=0;
}
static int debug_prompt(debug_state *d,char *line,u32 capacity)
{
    if(cli_text(1,"scdbg> ")<0)return -1;u32 used=0,tick=(u32)sc_tick();
    for(;;){debug_flush(d);u8 c;int r=sc_stream_read(d->terminal,&c,1);
        if(r==-6){if((u32)sc_tick()-tick>=5){tick=(u32)sc_tick();debug_state_read(d,1);}sc_yield();continue;}if(r<=0)return -1;
        if(c=='\r')continue;if(c=='\n'){line[used]=0;cli_text(1,"\n");return (int)used;}if(c==3 || c==27){used=0;cli_text(1,"\nscdbg> ");continue;}
        if(c==8 || c==127){if(used){used--;cli_text(1,"\b \b");}continue;}if(c<32 || c==127)continue;
        if(used+1>=capacity){cli_text(2,"\nscdbg: command capacity\n");return -2;}line[used++]=(char)c;if(cli_write(1,&c,1)<0)return -1;}
}
static int debug_main(void)
{
    if(cli_parse()<1){debug_help();return 2;}if(equal(cli_argv[0],"--help")){debug_help();return 0;}
    debug_state d={0};d.pid=d.input=d.terminal=-1;int at=0;
    if(equal(cli_argv[0],"--map")){if(cli_argc<3)return 2;int fd=sc_stream_open(cli_argv[1],1,0);if(fd<0)return cli_error("scdbg: map",fd);u32 size;char *map=cli_slurp(fd,16384,&size);sc_stream_close(fd,0);
        if(!map)return 1;for(u32 i=0;i<size;i++)if((u8)map[i]<32 && map[i]!='\n' && map[i]!='\r' && map[i]!='\t'){sc_free(map);return 2;}copy(d.map,map,sizeof(d.map));sc_free(map);at=2;}
    if(at>=cli_argc || !cli_argv[at][0] || length(cli_argv[at])>=64)return 2;
    /* SPAWN2的首词是路径，不支持引号；其余参数按CLI的引号合同
     * 重新序列化。路径含空白明确拒绝，不能启动另一个文件冒充目标。 */
    for(const char *p=cli_argv[at];*p;p++)if(*p==' ' || *p=='\t')return 2;
    copy(d.command,cli_argv[at++],sizeof(d.command));
    for(;at<cli_argc;at++){if(length(d.command)+2*length(cli_argv[at])+4>=(int)sizeof(d.command))return 2;append(d.command," \"",sizeof(d.command));
        for(const char *p=cli_argv[at];*p;p++){char text[3]={0,0,0};if(*p=='"'||*p=='\\'){text[0]='\\';text[1]=*p;}else text[0]=*p;append(d.command,text,sizeof(d.command));}append(d.command,"\"",sizeof(d.command));}
    d.terminal=sc_stream_open("",8,0);if(d.terminal<0)d.terminal=0;
    int r=debug_launch(&d);if(r<0){if(d.terminal>=3)sc_stream_close(d.terminal,0);return cli_error("scdbg: start",r);}debug_help();debug_registers(&d);
    char line[1024];
    for(;;){int got=debug_prompt(&d,line,sizeof(line));if(got==-2)continue;if(got<0)break;char *p=line,*op=token(&p);while(*p==' ')p++;if(!*op)continue;
        if(equal(op,"quit")||equal(op,"q"))break;if(equal(op,"help")){debug_help();continue;}
        if(equal(op,"restart")){debug_stop(&d);r=debug_launch(&d);if(r<0)cli_error("scdbg: restart",r);else debug_registers(&d);continue;}
        r=debug_state_read(&d,0);if(r<=0){cli_text(2,"No active target; restart or quit\n");continue;}
        if(equal(op,"status")){cli_text(1,d.paused?"Paused\n":"Running\n");continue;}
        if(equal(op,"regs")){debug_registers(&d);continue;}
        if(equal(op,"where")){if(d.context)debug_symbol(&d,d.frame[14]);else cli_text(2,"Target must be paused\n");continue;}
        if(equal(op,"input")){r=debug_input(&d,p);if(r<0)cli_error("scdbg: input queue/closed",r);continue;}
        if(equal(op,"eof")){d.input_eof=1;debug_flush(&d);continue;}
        if(equal(op,"breaks")){for(int i=0;i<d.break_count;i++){text_hex(1,d.breaks[i],8);cli_text(1,"\n");}continue;}
        r=-1;
        if(equal(op,"step")||equal(op,"s")){u32 count=1;if(*p && (text_number(p,&count)<0 || !count || count>10000)){cli_error("scdbg: step count",-1);continue;}
            for(u32 i=0;i<count;i++){r=debug_state_read(&d,0);if(r!=2)break;r=sc_debug(d.pid,1,0,0,0);if(r<0)break;u32 begin=(u32)sc_tick();
                do{r=debug_state_read(&d,0);if(r!=1)break;sc_yield();}while((u32)sc_tick()-begin<100);
                if(r!=2){if(r==1)cli_text(2,"Step still running; pause if needed\n");break;}}
            if(d.active && d.context)debug_registers(&d);
        }
        else if(equal(op,"cont")||equal(op,"c"))r=sc_debug(d.pid,2,0,0,0);
        else if(equal(op,"pause")){r=sc_debug(d.pid,7,0,0,0);if(r>=0){debug_state_read(&d,0);debug_registers(&d);}}
        else if(equal(op,"break")||equal(op,"clear")){u32 address;if(debug_hex(p,&address)<0)r=-1;else {r=sc_debug(d.pid,equal(op,"break")?4:5,address,0,0);
                if(r>=0){int found=-1;for(int i=0;i<d.break_count;i++)if(d.breaks[i]==address)found=i;
                    if(equal(op,"break") && found<0 && d.break_count<16)d.breaks[d.break_count++]=address;
                    else if(equal(op,"clear") && found>=0){for(int i=found+1;i<d.break_count;i++)d.breaks[i-1]=d.breaks[i];d.break_count--;}}}}
        else if(equal(op,"mem")||equal(op,"code")){u32 address=d.address,size=64;char *value=token(&p);if(*value && debug_hex(value,&address)<0)r=-1;
            else {value=token(&p);if(*value && text_number(value,&size)<0)r=-1;else r=equal(op,"code")?debug_code(&d,address,size):debug_memory(&d,address,size);}}
        else if(equal(op,"stack")){u32 count=16;if(!d.context || (*p && text_number(p,&count)<0) || !count || count>64)r=-1;
            else {r=0;for(u32 i=0;i<count;i++){u32 offset=i*4,value;if(d.frame[17]>0xFFFFFFFCu-offset){r=-1;break;}u32 address=d.frame[17]+offset;
                    text_hex(1,address,8);cli_text(1,"  ");if(sc_debug(d.pid,3,address,&value,4)<0)cli_text(1,"????????");else text_hex(1,value,8);cli_text(1,"\n");}}}
        if(r<0)cli_error("scdbg",r);
    }
    debug_stop(&d);if(d.terminal>=3)sc_stream_close(d.terminal,0);return 0;
}
