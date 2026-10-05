#include "../SCDEFLATE.H"
#include "../SCFILE.H"
#include "../SCGLOB.H"
typedef struct {char name[64];u32 crc,packed,bytes,offset,data,end,attributes;u16 method,flags;int directory;} zip_entry;
static zip_entry *entries;static u32 directory_start,archive_size;static int count,archive,list,test,pipe_output,overwrite,skip_existing,quiet;
static u32 limit,extracted;static const char *destination=".";static int selected_at;
static u32 le16(const u8 *p){return p[0]|(u32)p[1]<<8;}
static int exact_at(u32 at,void *out,u32 n)
{if(at>archive_size || n>archive_size-at || sc_stream_seek(archive,at)<0)return -1;u32 used=0;while(used<n){int r=cli_read(archive,(u8 *)out+used,n-used);if(r<=0)return -1;used+=(u32)r;}return 0;}
static int safe_name(char out[64],const u8 *raw,u32 bytes)
{
    while(bytes>=2 && raw[0]=='.' && raw[1]=='/'){raw+=2;bytes-=2;}
    if(!bytes || bytes>63 || raw[0]=='/' || raw[0]=='\\')return -1;
    for(u32 i=0;i<bytes;i++)if(!raw[i] || raw[i]=='\\' || raw[i]==':')return -1;
    u32 start=0;for(u32 i=0;i<=bytes;i++)if(i==bytes || raw[i]=='/'){
        if(i-start==2 && raw[start]=='.' && raw[start+1]=='.')return -1;start=i+1;}
    for(u32 i=0;i<bytes;i++)out[i]=(char)raw[i];out[bytes]=0;return 0;
}
static int inspect(void)
{
    u32 tail_size=archive_size<65557?archive_size:65557;u8 *tail=sc_alloc(tail_size);if(!tail)return -4;
    if(exact_at(archive_size-tail_size,tail,tail_size)<0){sc_free(tail);return -1;}int found=-1;
    for(int i=(int)tail_size-22;i>=0;i--)if(sand_little(tail+i)==0x06054B50u && i+22+(int)le16(tail+i+20)==(int)tail_size){found=i;break;}
    if(found<0){sc_free(tail);return -1;}const u8 *end=tail+found;u32 central_size=sand_little(end+12);directory_start=sand_little(end+16);count=(int)le16(end+10);
    u32 end_at=archive_size-tail_size+(u32)found;
    if(le16(end+4) || le16(end+6) || le16(end+8)!=(u32)count || count>1024 || directory_start>end_at || central_size!=end_at-directory_start){sc_free(tail);return -2;}sc_free(tail);
    entries=sc_alloc((u32)(count?count:1)*sizeof(zip_entry));if(!entries)return -4;u32 at=directory_start;
    for(int i=0;i<count;i++){u8 h[46];if(exact_at(at,h,46)<0 || sand_little(h)!=0x02014B50u)return -1;
        u32 names=le16(h+28),extra=le16(h+30),comment=le16(h+32),span=46+names+extra+comment;
        if(at>directory_start+central_size || span>directory_start+central_size-at || names>255 || !names || le16(h+34))return -2;
        u8 raw[256];if(exact_at(at+46,raw,names)<0)return -1;zip_entry *e=&entries[i];
        if(safe_name(e->name,raw,names)<0)return -2;e->directory=e->name[length(e->name)-1]=='/';
        e->flags=(u16)le16(h+8);e->method=(u16)le16(h+10);e->crc=sand_little(h+16);e->packed=sand_little(h+20);e->bytes=sand_little(h+24);e->offset=sand_little(h+42);e->attributes=sand_little(h+38);
        /* 不接受加密/分卷/ZIP64/符号链接。Unix外部属性不能让SandFS
         * 产生一个归档指定SYSTEM所有者或绕过当前任务路径保护。 */
        if((e->flags&~0x080Eu) || (e->flags&1) || (e->method!=0 && e->method!=8) || e->bytes>limit || e->offset>=directory_start
            || ((h[5]==3) && ((e->attributes>>16)&0170000u)==0120000u)
            || (e->directory && (e->bytes || e->packed || e->crc)) || (e->method==0 && e->packed!=e->bytes))return -2;
        u8 local[30];if(exact_at(e->offset,local,30)<0 || sand_little(local)!=0x04034B50u || le16(local+6)!=e->flags || le16(local+8)!=e->method || le16(local+26)!=names)return -1;
        if(!(e->flags&8) && (sand_little(local+14)!=e->crc || sand_little(local+18)!=e->packed || sand_little(local+22)!=e->bytes))return -1;
        u32 prefix=30+names+le16(local+28);if(prefix>directory_start-e->offset)return -1;e->data=e->offset+prefix;
        if(e->packed>directory_start-e->data)return -1;e->end=e->data+e->packed;
        u8 local_name[256];if(exact_at(e->offset+30,local_name,names)<0)return -1;for(u32 k=0;k<names;k++)if(local_name[k]!=raw[k])return -1;
        at+=span;
    }
    if(at!=directory_start+central_size)return -1;
    /* 至多1024条，一次插入排序检查区间；不在数据块热路径做两两扫描。 */
    int order[1024];for(int i=0;i<count;i++){int j=i;while(j && entries[order[j-1]].offset>entries[i].offset){order[j]=order[j-1];j--;}order[j]=i;}
    for(int i=1;i<count;i++)if(entries[order[i]].offset<entries[order[i-1]].end)return -1;return 0;
}
static int parents(char *path,int include)
{for(int i=0;;i++){char c=path[i];if(c=='/' || (!c && include)){path[i]=0;u32 info[2];int r=sc_stat(path,info);if(r<0)r=sc_mkdir(path);else if(info[0]!=2)r=-1;path[i]=c;if(r<0)return r;}if(!c)break;}return 0;}
static int chosen(const char *name)
{if(selected_at==cli_argc)return 1;for(int i=selected_at;i<cli_argc;i++)if(glob_match(name,cli_argv[i]))return 1;return 0;}
static int confirm(const char *path)
{
    if(overwrite)return 1;if(skip_existing)return 0;cli_text(2,"replace ");cli_text(2,path);cli_text(2,"? [y/n/A/N] ");
    u8 c=0,answer=0;while(cli_read(0,&c,1)==1){if(c=='\n')break;if(c!='\r' && !answer)answer=c;}
    if(answer=='A'){overwrite=1;return 1;}if(answer=='N'){skip_existing=1;return 0;}return answer=='y'||answer=='Y';
}
static int member(zip_entry *e)
{
    if(list){text_unsigned(1,e->bytes);cli_text(1," ");cli_text(1,e->name);cli_text(1,"\n");return 0;}
    int output=test?-1:pipe_output?1:-1;char path[64],relative[64],base[64];copy(relative,e->name,64);int n=length(relative);if(e->directory)relative[n-1]=0;
    if(!test && !pipe_output){if(cli_path(destination,base)<0 || file_join(path,base,relative)<0 || parents(path,e->directory)<0)return -1;
        if(e->directory)return 0;u32 old[2];if(sc_stat(path,old)>=0){if(old[0]!=1)return -1;if(!confirm(path))return 0;}
        char rooted[65];rooted[0]='/';copy(rooted+1,path,64);output=sc_stream_open(rooted,2,e->bytes);if(output<0)return output;
    }else if(e->directory)return 0;
    if(e->bytes>limit-extracted){if(output>2)sc_stream_close(output,0);return -2;}
    if(sc_stream_seek(archive,e->data)<0){if(output>2)sc_stream_close(output,0);return -1;}
    sand_reader r;sand_reader_init(&r,archive,e->packed,1);u32 crc=0,bytes=0;int result=0;
    if(e->method==8)result=sand_inflate(&r,output,e->bytes,&crc,&bytes);
    else {u8 buffer[4096];u32 left=e->bytes,raw_crc=0xFFFFFFFFu;while(left){u32 take=left>4096?4096:left;if(sand_reader_exact(&r,buffer,take)<0 || (output>=0 && cli_write(output,buffer,take)<0)){result=-1;break;}raw_crc=sand_crc(raw_crc,buffer,take);left-=take;bytes+=take;}crc=raw_crc^0xFFFFFFFFu;}
    if(bytes!=e->bytes || crc!=e->crc || r.remaining || r.at!=r.used)result=-1;
    if(output>2){int close=sc_stream_close(output,!result);if(close<0){sc_stream_close(output,0);result=close;}}
    if(!result){extracted+=bytes;if(!quiet && !pipe_output){cli_text(2,test?"checked: ":"extracted: ");cli_text(2,e->name);cli_text(2,"\n");}}return result;
}
int main(void)
{
    if(cli_parse()<1)return 2;int at=0;limit=sand_output_limit();
    while(at<cli_argc && cli_argv[at][0]=='-' && cli_argv[at][1]){char *option=cli_argv[at++];if(equal(option,"--"))break;
        if(equal(option,"-d")){if(at==cli_argc)return 2;destination=cli_argv[at++];continue;}
        for(int i=1;option[i];i++){char c=option[i];if(c=='l')list=1;else if(c=='t')test=1;else if(c=='p')pipe_output=quiet=1;else if(c=='o')overwrite=1;else if(c=='n')skip_existing=1;else if(c=='q')quiet=1;else return 2;}}
    if(at==cli_argc || list+test+pipe_output>1 || (overwrite && skip_existing))return 2;
    char resolved[64];u32 info[2];if(cli_path(cli_argv[at],resolved)<0 || sc_stat(resolved,info)<0 || info[0]!=1 || info[1]<22)return 1;
    archive_size=info[1];archive=sc_stream_open(cli_argv[at++],1,0);if(archive<0)return 1;selected_at=at;
    int result=inspect();if(!result)for(int i=0;i<count;i++)if(chosen(entries[i].name) && member(&entries[i])<0){result=-1;break;}
    if(entries)sc_free(entries);sc_stream_close(archive,0);return result<0?cli_error("unzip",result):0;
}
