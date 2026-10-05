#include "SCCRONTAB.H"
static int cron_number(const char **from,int *out)
{const char *p=*from;int n=0;if(*p<'0'||*p>'9')return -1;while(*p>='0'&&*p<='9'){n=n*10+*p++-'0';if(n>999)return -1;}*from=p;*out=n;return 0;}
static int cron_field(const char *word,int low,int high,u32 bits[2])
{
    bits[0]=bits[1]=0;const char *p=word;for(;;){int begin,end,step=1;
        if(*p=='*'){p++;begin=low;end=high;}else {if(cron_number(&p,&begin)<0)return -1;end=begin;if(*p=='-'){p++;if(cron_number(&p,&end)<0)return -1;}}
        if(*p=='/'){p++;if(cron_number(&p,&step)<0 || !step || step>high-low+1)return -1;}
        if(begin<low || end>high || end<begin)return -1;for(int n=begin;n<=end;n+=step){int bit=high==7 && n==7?0:n;bits[bit>>5]|=1u<<(bit&31);}
        if(!*p)return 0;if(*p++!=',' || !*p)return -1;
    }
}
static int cron_parse(char *text,u32 bytes,cron_table *out)
{
    static const int low[5]={0,0,1,1,0},high[5]={59,23,31,12,7};out->count=0;u32 at=0;
    for(u32 i=0;i<bytes;i++)if(!text[i])return -1;
    while(at<bytes){u32 end=at;while(end<bytes && text[end]!='\n')end++;u32 next=end<bytes?end+1:end;if(end>at && text[end-1]=='\r')end--;
        text[end]=0;char *p=text+at;while(*p==' '||*p=='\t')p++;if(!*p || *p=='#'){at=next;continue;}
        if(out->count==CRON_ROWS)return -1;cron_entry *entry=out->rows+out->count;
        for(int field=0;field<5;field++){char *word=p;while(*p && *p!=' ' && *p!='\t')p++;if(!*p)return -1;*p++=0;
            if(cron_field(word,low[field],high[field],entry->bits[field])<0)return -1;if(field==2)entry->day_any=*word=='*';if(field==4)entry->week_any=*word=='*';
            while(*p==' '||*p=='\t')p++;}
        int n=length(p);if(!n || n>=1024)return -1;copy(entry->command,p,1024);out->count++;at=next;
    }return 0;
}
static int cron_read(const char *path,cron_table *out,u32 *generation)
{
    char absolute[64];u32 meta[8];if(cli_path(path,absolute)<0)return -1;int r=sc_fsmeta(absolute,meta);if(r<0)return r;if(meta[1]!=1 || meta[2]>32767)return -1;
    int fd=sc_stream_open(path,1,0);if(fd<0)return fd;u32 bytes;char *text=cli_slurp(fd,32767,&bytes);sc_stream_close(fd,0);if(!text)return -1;
    u32 after[8];r=sc_fsmeta(absolute,after);if(r<0 || after[7]!=meta[7]){sc_free(text);return -8;}
    r=cron_parse(text,bytes,out);sc_free(text);if(!r)*generation=meta[7];return r;
}
static int cron_path(const char *directory,char out[64])
{
    /* 账户HOME沿用旧ABI的卷根相对形式；-c显式目录仍相对调用者cwd。 */
    char home[64],rooted[65];if(!directory){if(sc_user(1,"",home,sizeof(home))<0)return -1;rooted[0]='/';copy(rooted+1,home,64);directory=rooted;}
    char absolute[64];if(cli_path(directory,absolute)<0)return -1;int n=length(absolute);if(n+11>=64)return -1;out[0]='/';copy(out+1,absolute,63);if(n)append(out,"/",64);append(out,"CRON.TAB",64);return 0;
}
static int cron_bit(const cron_entry *entry,int field,int n)
{return !!(entry->bits[field][n>>5]&(1u<<(n&31)));}
static int cron_matches(const cron_entry *entry,const u32 time[8])
{
    int day=cron_bit(entry,2,(int)time[3]),week=cron_bit(entry,4,calendar_weekday((int)time[1],(int)time[2],(int)time[3]));
    return cron_bit(entry,0,(int)time[5]) && cron_bit(entry,1,(int)time[4]) && cron_bit(entry,3,(int)time[2])
        && (entry->day_any || entry->week_any?day&&week:day||week);
}
