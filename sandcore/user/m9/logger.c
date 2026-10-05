#include "../SCLOG.H"
#include "../SCCALENDAR.H"
#define LOG_LIMIT 65536u
static int append_bytes(char *out,u32 *at,const void *bytes,u32 n)
{if(n>LOG_LIMIT-*at)return -1;const char *p=bytes;for(u32 i=0;i<n;i++)out[(*at)++]=p[i];return 0;}
static int append_word(char *out,u32 *at,const char *word)
{return append_bytes(out,at,word,(u32)length(word));}
int main(void)
{
    if(cli_parse()<0)return 2;const char *tag="user";int priority=13,first=0;
    for(;first<cli_argc;first++){const char *option=cli_argv[first];if(equal(option,"--")){first++;break;}if(option[0]!='-')break;
        if(equal(option,"-t") && first+1<cli_argc){tag=cli_argv[++first];continue;}
        if(equal(option,"-p") && first+1<cli_argc){if(cli_integer(cli_argv[++first],&priority)<0 || priority<0 || priority>191)return 2;continue;}return 2;}
    int tag_size=length(tag);if(!tag_size || tag_size>32)return 2;for(int i=0;i<tag_size;i++)if((u8)tag[i]<=32 || tag[i]=='[' || tag[i]==']')return 2;
    u32 bytes=0;char *message=0;if(first==cli_argc)message=cli_slurp(0,32767,&bytes);
    else {message=sc_alloc(1024);if(message){for(int i=first;i<cli_argc;i++){int n=length(cli_argv[i]);if(bytes+(u32)n+(i>first?1u:0u)>1023){sc_free(message);message=0;break;}
                if(i>first)message[bytes++]=' ';for(int j=0;j<n;j++)message[bytes++]=cli_argv[i][j];}if(message)message[bytes]=0;}}
    if(!message)return cli_error("logger: input/size",-1);for(u32 i=0;i<bytes;i++)if(!message[i]){sc_free(message);return 2;}
    char path[64];if(local_log_path(path)<0){sc_free(message);return 1;}u32 own[8],clock[8];if(sc_process_self(own)<0 || calendar_read(clock)<0){sc_free(message);return 1;}
    char prefix[128]="[",number[12];for(int i=1;i<=6;i++){decimal(number,(int)clock[i]);append(prefix,number,sizeof(prefix));append(prefix,i==6?" uid=":i<3?"-":i==3?" ":":",sizeof(prefix));}
    decimal(number,(int)own[3]);append(prefix,number,sizeof(prefix));append(prefix," pid=",sizeof(prefix));decimal(number,(int)own[1]);append(prefix,number,sizeof(prefix));append(prefix," p=",sizeof(prefix));decimal(number,priority);append(prefix,number,sizeof(prefix));append(prefix,"] ",sizeof(prefix));append(prefix,tag,sizeof(prefix));append(prefix,": ",sizeof(prefix));
    char *fresh=sc_alloc(LOG_LIMIT);if(!fresh){sc_free(message);return 1;}int result=1;
    for(int tries=0;tries<8;tries++){u32 at=0,meta[8];char absolute[64];if(cli_path(path,absolute)<0)break;int r=sc_fsmeta(absolute,meta);
        if(r==-1){r=sc_create_ex(path,1,3);if(r==-8 || r==-6){sc_yield();continue;}if(r<0)break;if(sc_fsmeta(absolute,meta)<0)break;}
        else if(r<0)break;if(meta[1]!=1 || meta[2]>LOG_LIMIT)break;
        int fd=sc_stream_open(path,1,0);if(fd<0)break;while(at<meta[2]){r=cli_read(fd,fresh+at,meta[2]-at);if(r<=0)break;at+=(u32)r;}sc_stream_close(fd,0);if(at!=meta[2])continue;
        if(at && fresh[at-1]!='\n')break;
        u32 begin=0;int failed=0;while(begin<bytes){u32 end=begin;while(end<bytes && message[end]!='\n')end++;
            if(append_word(fresh,&at,prefix)<0){failed=1;break;}for(u32 i=begin;i<end;i++){u8 c=(u8)message[i];if(c<32 || c==127){const char *digits="0123456789abcdef";char escaped[4]={'\\','x',digits[c>>4],digits[c&15]};if(append_bytes(fresh,&at,escaped,4)<0){failed=1;break;}}
                else if(append_bytes(fresh,&at,&c,1)<0){failed=1;break;}}if(failed || append_word(fresh,&at,"\n")<0){failed=1;break;}begin=end+(end<bytes?1:0);}
        if(failed)break;fd=sc_stream_open(path,2,at);if(fd==-6){sc_yield();continue;}if(fd<0)break;
        u32 current[8];if(sc_fsmeta(absolute,current)<0 || current[7]!=meta[7]){sc_stream_close(fd,0);sc_yield();continue;}
        r=cli_write(fd,fresh,at);int end=sc_stream_close(fd,r>=0);if(end<0){sc_stream_close(fd,0);r=-1;}if(r>=0)result=0;break;
    }
    sc_free(fresh);sc_free(message);return result?cli_error("logger: append/size",-1):0;
}
