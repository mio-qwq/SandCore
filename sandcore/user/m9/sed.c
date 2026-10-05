#include "../SCRE.H"
/* 自写流编辑器。正则在解析脚本时编译一次，行处理直接引用NFA；
 * 单行/模式空间64KiB、脚本32KiB、64指令，容量不足明确失败。
 * 不使用BusyBox源码或宿主regex/libc。替换支持&，反向引用未实现。 */
typedef struct {int kind;u32 line;re_program pattern;} sed_address;
typedef struct {sed_address first,last;int invert,active,entering,op,target,global,print,occurrence;re_program pattern;char *text;char label[32];} sed_command;
static sed_command commands[64];static int count,quiet,extended,in_place,needs_last,output=1,argument,input_fd,input_opened,finished;
static char script[32768];static u32 script_size,line_number;static text_reader reader;static text_line hold;
static void spaces(char **p){while(**p==' ' || **p=='\t')(*p)++;}
static int add_script(const char *text,u32 bytes)
{if(bytes+script_size+2>sizeof(script))return -1;for(u32 i=0;i<bytes;i++)script[script_size++]=text[i];script[script_size++]='\n';script[script_size]=0;return 0;}
static char *piece(char **cursor,char delimiter,int *error)
{
    char *p=*cursor;u32 n=0;while(p[n] && p[n]!=delimiter){if(p[n]=='\\' && p[n+1])n++;n++;}if(!p[n]){*error=1;return 0;}
    char *out=sc_alloc(n+1);if(!out){*error=1;return 0;}for(u32 i=0;i<n;i++)out[i]=p[i];out[n]=0;*cursor=p+n+1;return out;
}
static int address(char **cursor,sed_address *out)
{
    char *p=*cursor;if(*p>='0'&&*p<='9'){u32 n=0;do{u32 digit=(u32)(*p++-'0');if(n>(0xFFFFFFFFu-digit)/10)return -1;n=n*10+digit;}while(*p>='0'&&*p<='9');if(!n)return -1;out->kind=1;out->line=n;}
    else if(*p=='$'){p++;out->kind=2;}
    else if(*p=='/'){p++;int error=0;char *pattern=piece(&p,'/',&error);if(error)return -1;int r=re_program_compile(&out->pattern,pattern,extended,0);sc_free(pattern);if(r<0)return -1;out->kind=3;}
    else return 0;*cursor=p;return 1;
}
static int parse_script(void)
{
    char *p=script;int groups[32],depth=0;
    while(*p){while(*p==' '||*p=='\t'||*p=='\n'||*p==';')p++;if(!*p)break;if(*p=='#'){while(*p && *p!='\n')p++;continue;}if(count==64)return -1;
        sed_command *c=&commands[count];int r=address(&p,&c->first);if(r<0)return -1;spaces(&p);
        if(*p==','){if(!r)return -1;p++;spaces(&p);if(address(&p,&c->last)<=0)return -1;spaces(&p);}
        if(c->first.kind==2 || c->last.kind==2)needs_last=1;
        if(*p=='!'){c->invert=1;p++;spaces(&p);}if(!*p)return -1;c->op=*p++;c->target=-1;
        if(c->op=='s'){char delimiter=*p++;if(!delimiter || delimiter=='\n' || delimiter=='\\')return -1;int error=0;
            char *pattern=piece(&p,delimiter,&error);c->text=piece(&p,delimiter,&error);if(error){if(pattern)sc_free(pattern);return -1;}
            for(int i=0;c->text[i];i++)if(c->text[i]=='\\' && c->text[i+1]){if(c->text[i+1]>='1'&&c->text[i+1]<='9'){sc_free(pattern);return -1;}i++;}
            int fold=0;while(*p && *p!=';' && *p!='\n' && *p!=' ' && *p!='\t'){
                if(*p=='g')c->global=1;else if(*p=='p')c->print=1;else if(*p=='i'||*p=='I')fold=1;
                else if(*p>='1'&&*p<='9'){int n=0;while(*p>='0'&&*p<='9'){n=n*10+(*p++-'0');if(n>99){sc_free(pattern);return -1;}}c->occurrence=n;continue;}
                else {sc_free(pattern);return -1;}p++;}
            r=re_program_compile(&c->pattern,pattern,extended,fold);sc_free(pattern);if(r<0)return -1;
        }else if(c->op=='a'||c->op=='i'||c->op=='c'){
            spaces(&p);if(*p=='\\'){p++;if(*p=='\n')p++;}char *begin=p;while(*p && *p!='\n')p++;u32 n=(u32)(p-begin);c->text=sc_alloc(n+1);if(!c->text)return -1;for(u32 i=0;i<n;i++)c->text[i]=begin[i];c->text[n]=0;
        }else if(c->op==':'||c->op=='b'||c->op=='t'){
            spaces(&p);int n=0;while(*p && *p!=';' && *p!='\n' && *p!=' ' && *p!='\t'){if(n==31)return -1;c->label[n++]=*p++;}c->label[n]=0;if(c->op==':' && !n)return -1;
        }else if(c->op=='{'){if(depth==32)return -1;groups[depth++]=count;}
        else if(c->op=='}'){if(!depth || c->first.kind)return -1;commands[groups[--depth]].target=count+1;}
        else if(c->op!='p'&&c->op!='P'&&c->op!='d'&&c->op!='D'&&c->op!='q'&&c->op!='n'&&c->op!='N'&&c->op!='h'&&c->op!='H'&&c->op!='g'&&c->op!='G'&&c->op!='x'&&c->op!='='&&c->op!='l')return -1;
        spaces(&p);if(*p && *p!=';' && *p!='\n' && c->op!='{')return -1;count++;
    }
    if(depth)return -1;for(int i=0;i<count;i++)if(commands[i].op=='b'||commands[i].op=='t'){
        if(!*commands[i].label){commands[i].target=count;continue;}for(int j=0;j<count;j++)if(commands[j].op==':' && equal(commands[j].label,commands[i].label))commands[i].target=j;
        if(commands[i].target<0)return -1;}return 0;
}
static int match_address(sed_address *a,const text_line *line,int last)
{u32 begin,end;return !a->kind?1:a->kind==1?line_number==a->line:a->kind==2?last:re_program_search(&a->pattern,line->bytes,line->size,0,&begin,&end);}
static int selected(sed_command *c,const text_line *line,int last)
{
    c->entering=0;int yes;if(c->last.kind){yes=c->active;
        if(!yes && match_address(&c->first,line,last)){c->active=1;c->entering=1;yes=1;}
        if(yes && ((c->last.kind==1 && line_number>=c->last.line) || (c->last.kind!=3 || !c->entering) && match_address(&c->last,line,last)))c->active=0;
    }else yes=match_address(&c->first,line,last);return yes^c->invert;
}
static int resize(text_line *line,u32 needed)
{if(needed>65536)return -1;if(needed<=line->capacity)return 0;u32 capacity=line->capacity?line->capacity:256;while(capacity<needed)capacity*=2;
    char *p=sc_alloc(capacity);if(!p)return -1;for(u32 i=0;i<line->size;i++)p[i]=line->bytes[i];if(line->bytes)sc_free(line->bytes);line->bytes=p;line->capacity=capacity;return 0;}
static int line_copy(text_line *out,const text_line *in,int append_line)
{u32 old=append_line?out->size:0;if(old>65535 || in->size>65536-old-(append_line?1:0) || resize(out,old+in->size+(append_line?1:0))<0)return -1;
    if(append_line)out->bytes[old++]='\n';for(u32 i=0;i<in->size;i++)out->bytes[old+i]=in->bytes[i];out->size=old+in->size;out->newline=in->newline;return 0;}
static int replace(sed_command *c,text_line *line)
{
    text_line out={0,0,0,line->newline};u32 offset=0,copied=0,begin,end;int occurrence=0,changed=0,last_empty=-1;
    while(re_program_search(&c->pattern,line->bytes,line->size,offset,&begin,&end)){
        if(begin==end && (int)begin==last_empty){if(begin==line->size)break;offset=begin+1;continue;}occurrence++;
        int use=c->occurrence?occurrence==c->occurrence || (c->global && occurrence>c->occurrence):!changed || c->global;
        if(use){if(resize(&out,out.size+begin-copied)<0)goto fail;for(u32 i=copied;i<begin;i++)out.bytes[out.size++]=line->bytes[i];
            for(int i=0;c->text[i];i++){char value=c->text[i];if(value=='&'){if(resize(&out,out.size+end-begin)<0)goto fail;for(u32 j=begin;j<end;j++)out.bytes[out.size++]=line->bytes[j];}
                else {if(value=='\\' && c->text[i+1]){value=c->text[++i];if(value=='n')value='\n';else if(value=='t')value='\t';}
                    if(resize(&out,out.size+1)<0)goto fail;out.bytes[out.size++]=value;}}
            copied=end;changed=1;if(!c->global)break;
        }
        if(begin==end){last_empty=(int)begin;if(begin==line->size)break;offset=end+1;}else {last_empty=(int)end;offset=end;}
    }
    if(!changed){text_line_free(&out);return 0;}if(resize(&out,out.size+line->size-copied)<0)goto fail;for(u32 i=copied;i<line->size;i++)out.bytes[out.size++]=line->bytes[i];
    text_line_free(line);*line=out;if(c->print && text_line_write(output,line)<0)return -1;return 1;
fail:text_line_free(&out);return -1;
}
static int input_line(text_line *line)
{
    for(;;){if(!input_opened){if(argument==cli_argc){if(finished)return 0;input_fd=0;finished=1;}else {input_fd=cli_input(argument++);if(input_fd<0)return -1;}
            reader=(text_reader){input_fd,{0},0,0};input_opened=1;}
        int n=text_line_read(&reader,line);if(n)return n>0?n:-1;if(input_fd)sc_stream_close(input_fd,0);input_opened=0;
        if(argument==cli_argc)return 0;
    }
}
static int append_flush(int *pending,int amount)
{for(int i=0;i<amount;i++)if(cli_text(output,commands[pending[i]].text)<0 || cli_text(output,"\n")<0)return -1;return 0;}
static int edit_stream(void)
{
    text_line line={0,0,0,0},next={0,0,0,0};int ready=input_line(&line),result=0;if(ready<0)return -1;line_number=0;
    while(ready>0){line_number++;int look=needs_last?input_line(&next):-2;if(look==-1){result=-1;break;}int deleted=0,quit=0,changed=0,pending[64],amount=0;u32 steps=0;
        for(int pc=0;pc<count && !deleted && !quit;pc++){
            if(!(++steps&1023))sc_yield();sed_command *c=&commands[pc];int yes=selected(c,&line,look==0);if(c->op=='{' && !yes){pc=c->target-1;continue;}if(!yes)continue;
            if(c->op=='s'){int n=replace(c,&line);if(n<0){result=-1;goto done;}if(n)changed=1;}
            else if(c->op=='p'){if(text_line_write(output,&line)<0){result=-1;goto done;}}
            else if(c->op=='P'){u32 n=0;while(n<line.size && line.bytes[n]!='\n')n++;if(cli_write(output,line.bytes,n)<0 || cli_text(output,"\n")<0){result=-1;goto done;}}
            else if(c->op=='d')deleted=1;
            else if(c->op=='D'){u32 n=0;while(n<line.size && line.bytes[n]!='\n')n++;if(n==line.size)deleted=1;else {n++;for(u32 i=n;i<line.size;i++)line.bytes[i-n]=line.bytes[i];line.size-=n;pc=-1;}}
            else if(c->op=='q')quit=1;
            else if(c->op=='b')pc=c->target-1;
            else if(c->op=='t'){int branch=changed;changed=0;if(branch)pc=c->target-1;}
            else if(c->op=='a'){if(amount==64){result=-1;goto done;}pending[amount++]=pc;}
            else if(c->op=='i' || c->op=='c'){if(c->op=='i' || !c->last.kind || c->entering){if(cli_text(output,c->text)<0 || cli_text(output,"\n")<0){result=-1;goto done;}}if(c->op=='c')deleted=1;}
            else if(c->op=='h'||c->op=='H'){if(line_copy(&hold,&line,c->op=='H')<0){result=-1;goto done;}}
            else if(c->op=='g'||c->op=='G'){if(line_copy(&line,&hold,c->op=='G')<0){result=-1;goto done;}}
            else if(c->op=='x'){text_line swap=line;line=hold;hold=swap;}
            else if(c->op=='='){text_unsigned(output,line_number);if(cli_text(output,"\n")<0){result=-1;goto done;}}
            else if(c->op=='l'){for(u32 i=0;i<line.size;i++){u8 value=(u8)line.bytes[i];if(value=='\n')cli_text(output,"\\n");else if(value=='\t')cli_text(output,"\\t");else if(value=='\\')cli_text(output,"\\\\");else if(value<32 || value>=127){cli_text(output,"\\x");text_hex(output,value,2);}else if(cli_write(output,&value,1)<0){result=-1;goto done;}}if(cli_text(output,"$\n")<0){result=-1;goto done;}}
            else if(c->op=='n'||c->op=='N'){
                if(look==-2){look=input_line(&next);if(look<0){result=-1;goto done;}}
                if(c->op=='n'){if(!quiet && text_line_write(output,&line)<0){result=-1;goto done;}if(append_flush(pending,amount)<0){result=-1;goto done;}amount=0;}
                if(!look){if(c->op=='n')deleted=1;quit=1;break;}
                if(c->op=='N'){if(line_copy(&line,&next,1)<0){result=-1;goto done;}}
                else {text_line swap=line;line=next;next=swap;changed=0;}
                line_number++;look=needs_last?input_line(&next):-2;if(look==-1){result=-1;goto done;}
            }
        }
        if(!deleted && !quiet && text_line_write(output,&line)<0){result=-1;break;}if(append_flush(pending,amount)<0){result=-1;break;}if(quit)break;
        if(look==-2){look=input_line(&next);if(look<0){result=-1;break;}}text_line swap=line;line=next;next=swap;ready=look;
    }
done:text_line_free(&line);text_line_free(&next);return result;
}
static void cleanup(void)
{for(int i=0;i<64;i++){re_program_free(&commands[i].first.pattern);re_program_free(&commands[i].last.pattern);re_program_free(&commands[i].pattern);if(commands[i].text)sc_free(commands[i].text);}text_line_free(&hold);if(input_opened && input_fd)sc_stream_close(input_fd,0);}
int main(void)
{
    if(cli_parse()<1)return 2;int at=0,specified=0;while(at<cli_argc && cli_argv[at][0]=='-' && cli_argv[at][1]){
        char *option=cli_argv[at++];if(equal(option,"--"))break;
        if(equal(option,"-n"))quiet=1;else if(equal(option,"-E")||equal(option,"-r"))extended=1;else if(equal(option,"-i"))in_place=1;
        else if(equal(option,"-e")){if(at==cli_argc || add_script(cli_argv[at],(u32)length(cli_argv[at]))<0)return 2;at++;specified=1;}
        else if(equal(option,"-f")){if(at==cli_argc)return 2;int fd=sc_stream_open(cli_argv[at++],1,0);if(fd<0)return 2;u32 n;char *text=cli_slurp(fd,32767,&n);sc_stream_close(fd,0);if(!text)return 2;int r=add_script(text,n);sc_free(text);if(r<0)return 2;specified=1;}
        else return 2;
    }
    if(!specified){if(at==cli_argc || add_script(cli_argv[at],(u32)length(cli_argv[at]))<0)return 2;at++;}
    if(parse_script()<0){cleanup();return cli_error("sed: script",-1);}int result=0;
    if(in_place){if(at==cli_argc){cleanup();return 2;}int last_argument=cli_argc;for(int i=at;i<last_argument;i++){
            if(equal(cli_argv[i],"-")){result=1;break;}output=sc_stream_open(cli_argv[i],2,16777216);if(output<0){result=1;break;}
            input_opened=finished=0;argument=i;cli_argc=i+1;for(int j=0;j<count;j++)commands[j].active=0;
            int r=edit_stream();if(input_opened && input_fd)sc_stream_close(input_fd,0);input_opened=0;text_line_free(&hold);hold=(text_line){0,0,0,0};
            if(sc_stream_close(output,r==0)<0){sc_stream_close(output,0);r=-1;}cli_argc=last_argument;if(r<0){result=1;break;}}
    }else {argument=at;finished=at<cli_argc;result=edit_stream()<0;}
    cleanup();return result;
}
