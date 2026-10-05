#include "../SCPROMPT.H"
#include "../SCTEXT.H"
/* 串口友好的自写行编辑器：1,5p / a / i / c / d / s/old/new/g /
 * r file / w [file] / q / Q。正文不在每次按键重写磁盘，w一次性
 * 发布COW事务；失败保留原文件及内存中的未保存内容。 */
#define ED_LINES 8192
typedef struct {char *text;u32 size;int newline;} ed_line;
static ed_line *lines;static u32 count,current,total;static int changed;
static char filename[64];
static void release_line(ed_line l){if(l.text)sc_free(l.text);}
static int insert(u32 at,const char *text,u32 size,int newline)
{
    if(at>count || count==ED_LINES || total>0x1000000u-size)return -1;
    char *copy_text=sc_alloc(size+1);if(!copy_text)return -1;for(u32 i=0;i<size;i++)copy_text[i]=text[i];copy_text[size]=0;
    for(u32 i=count;i>at;i--)lines[i]=lines[i-1];lines[at]=(ed_line){copy_text,size,newline};count++;total+=size;current=at+1;changed=1;return 0;
}
static void remove_range(u32 first,u32 last)
{
    for(u32 i=first;i<=last;i++){total-=lines[i].size;release_line(lines[i]);}
    u32 n=last-first+1;for(u32 i=last+1;i<count;i++)lines[i-n]=lines[i];count-=n;current=first<count?first+1:count;changed=1;
}
static int load_file(const char *name,u32 at)
{
    int fd=sc_stream_open(name,1,0);if(fd<0)return fd;text_reader reader={fd,{0},0,0};text_line line={0,0,0,0};int n;
    while((n=text_line_read(&reader,&line))>0){if(insert(at++,line.bytes,line.size,line.newline)<0){n=-1;break;}}
    text_line_free(&line);sc_stream_close(fd,0);return n<0?n:0;
}
static int address(char **cursor,u32 *out)
{
    char *p=*cursor;u32 n=0;
    if(*p=='.'){n=current;p++;}else if(*p=='$'){n=count;p++;}
    else if(*p>='0' && *p<='9'){while(*p>='0'&&*p<='9'){if(n>ED_LINES)return -1;n=n*10+(u32)(*p++-'0');}}
    else return 0;
    while(*p=='+' || *p=='-'){int minus=*p++=='-';u32 d=0;int digits=0;while(*p>='0'&&*p<='9'){if(d>ED_LINES)return -1;d=d*10+(u32)(*p++-'0');digits=1;}if(!digits)d=1;
        if(minus){if(d>n)return -1;n-=d;}else n+=d;}
    if(n>count)return -1;*cursor=p;*out=n;return 1;
}
static int save(const char *name)
{
    u32 bytes=total;for(u32 i=0;i<count;i++)bytes+=(u32)lines[i].newline;int fd=sc_stream_open(name,2,bytes);if(fd<0)return fd;
    for(u32 i=0;i<count;i++)if(cli_write(fd,lines[i].text,lines[i].size)<0 || (lines[i].newline && cli_text(fd,"\n")<0)){sc_stream_close(fd,0);return -1;}
    int r=sc_stream_close(fd,1);if(r<0){sc_stream_close(fd,0);return r;}changed=0;text_unsigned(1,bytes);cli_text(1,"\n");return 0;
}
static int substitute(ed_line *line,const char *old,const char *replacement,int global)
{
    u32 old_size=(u32)length(old),new_size=(u32)length(replacement);if(!old_size)return -1;
    char *out=sc_alloc(65537);if(!out)return -1;u32 used=0;int found=0;
    for(u32 at=0;at<line->size;){int match=(!found || global) && old_size<=line->size-at && !text_compare(line->text+at,old_size,old,old_size,0);
        u32 n=match?new_size:1;if(n>65536-used){sc_free(out);return -1;}const char *from=match?replacement:line->text+at;
        for(u32 i=0;i<n;i++)out[used++]=from[i];at+=match?old_size:1;if(match)found=1;}
    if(!found){sc_free(out);return 1;}if(total-line->size>0x1000000u-used){sc_free(out);return -1;}
    out[used]=0;total=total-line->size+used;sc_free(line->text);line->text=out;line->size=used;changed=1;return 0;
}
int main(void)
{
    if(cli_parse()<0 || cli_argc>1)return 2;lines=sc_alloc(ED_LINES*sizeof(ed_line));if(!lines)return 1;
    if(cli_argc){if(length(cli_argv[0])>=64)return 2;copy(filename,cli_argv[0],64);u32 meta[2];char path[64];
        if(cli_path(filename,path)<0)return 2;if(sc_stat(path,meta)>=0 && load_file(filename,0)<0)return cli_error("ed: read",-1);changed=0;}
    char command[1024],content[1024];int quitting=0,result=0;cli_text(1,"SandEd: a/i/c, . ends input; w saves, q quits\n");
    while(!quitting){if(prompt_line(": ",command,sizeof(command),0)<0){result=changed?1:0;break;}
        char *p=command;u32 first=current,last=current;int has=address(&p,&first);if(has<0){cli_text(2,"? address\n");continue;}last=first;
        if(*p==','){p++;if(address(&p,&last)<=0 || first>last){cli_text(2,"? range\n");continue;}}else if(*p=='%'){p++;first=1;last=count;has=1;}
        while(*p==' ')p++;char op=*p++;if(!op){op='p';if(!has){first=current+1;last=first;}p--;}
        while(*p==' ')p++;
        if(op=='q' || op=='Q'){if(op=='q' && changed){cli_text(2,"? unsaved; w or Q\n");continue;}quitting=1;continue;}
        if(op=='w'){const char *target=*p?p:filename;if(!*target || save(target)<0){cli_text(2,"? write\n");continue;}if(*p)copy(filename,p,64);continue;}
        if(op=='r'){if(!*p || load_file(p,last)<0)cli_text(2,"? read\n");continue;}
        if(op=='a' || op=='i' || op=='c'){
            if((op!='a' && (!first || first>count)) || last>count){cli_text(2,"? address\n");continue;}
            u32 at=op=='a'?last:first-1;if(op=='c')remove_range(first-1,last-1);
            while(prompt_line("",content,sizeof(content),0)>=0 && !equal(content,".")){if(insert(at++,content,(u32)length(content),1)<0){cli_text(2,"? capacity\n");break;}}
            continue;
        }
        if(!first || last>count){cli_text(2,"? address\n");continue;}
        if(op=='p' || op=='n'){for(u32 i=first-1;i<last;i++){if(op=='n'){text_unsigned(1,i+1);cli_text(1,"\t");}cli_write(1,lines[i].text,lines[i].size);cli_text(1,"\n");}current=last;}
        else if(op=='d')remove_range(first-1,last-1);
        else if(op=='s'){
            char delimiter=*p++;if(!delimiter){cli_text(2,"? substitution\n");continue;}char *old=p;while(*p && *p!=delimiter)p++;
            if(!*p){cli_text(2,"? substitution\n");continue;}*p++=0;char *replacement=p;while(*p && *p!=delimiter)p++;
            if(!*p){cli_text(2,"? substitution\n");continue;}*p++=0;int success=0;
            for(u32 i=first-1;i<last;i++){int r=substitute(&lines[i],old,replacement,*p=='g');if(r<0){success=-1;break;}if(!r)success=1;}
            if(success<=0)cli_text(2,"? no match/capacity\n");current=last;
        }else cli_text(2,"? command\n");
    }
    for(u32 i=0;i<count;i++)release_line(lines[i]);sc_free(lines);return result;
}
