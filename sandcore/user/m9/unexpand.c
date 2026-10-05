#include "../SCTEXT.H"
static u32 stops[64],stop_count,interval=8;
static int all_blanks;
static int tab_stops(const char *text)
{
    u32 n=0;int digits=0;while(*text){char c=*text++;if(c>='0'&&c<='9'){u32 d=(u32)(c-'0');if(n>(65536-d)/10)return -1;n=n*10+d;digits=1;continue;}
        if((c!=',' && c!=' ') || !digits || !n || stop_count==64 || (stop_count && n<=stops[stop_count-1]))return -1;stops[stop_count++]=n;n=0;digits=0;}
    if(!digits || !n || stop_count==64 || (stop_count && n<=stops[stop_count-1]))return -1;stops[stop_count++]=n;if(stop_count==1){interval=n;stop_count=0;}return 0;
}
static u32 next_stop(u32 column)
{if(!stop_count)return column>0xFFFFFFFFu-interval?0xFFFFFFFFu:column+interval-column%interval;
    for(u32 i=0;i<stop_count;i++)if(stops[i]>column)return stops[i];return column==0xFFFFFFFFu?column:column+1;}
typedef struct {u8 bytes[4096];u32 used;} unexpand_output;
static int unexpand_flush(unexpand_output *out)
{int n=cli_write(1,out->bytes,out->used);out->used=0;return n<0?-1:0;}
static int unexpand_byte(unexpand_output *out,u8 c)
{if(out->used==sizeof(out->bytes) && unexpand_flush(out)<0)return -1;out->bytes[out->used++]=c;return 0;}
static int unexpand_blanks(unexpand_output *out,u32 from,u32 to,int convert)
{
    while(from<to){u32 stop=next_stop(from);if(convert && stop<=to && stop-from>1){if(unexpand_byte(out,'\t')<0)return -1;from=stop;}
        else {if(unexpand_byte(out,' ')<0)return -1;from++;}}return 0;
}
static int unexpand_run(int fd,const char *name)
{
    (void)name;u8 bytes[4096];unexpand_output out={{0},0};u32 column=0,pending=0;int leading=1;
    for(;;){int n=cli_read(fd,bytes,sizeof(bytes));if(n<0)return 1;if(!n)break;
        for(int i=0;i<n;i++){u8 c=bytes[i];if(c==' ' || c=='\t'){if(column==0xFFFFFFFFu)return cli_error("unexpand: column overflow",-1);
                if(!leading && !all_blanks){u32 next=c=='\t'?next_stop(column):column+1;if(next<=column || unexpand_byte(&out,c)<0)return 1;column=next;continue;}if(!pending)pending=column+1;
                u32 next=c=='\t'?next_stop(column):column+1;if(next<=column)return 1;column=next;continue;}
            if(pending){if(unexpand_blanks(&out,pending-1,column,leading || all_blanks)<0)return 1;pending=0;}
            if(unexpand_byte(&out,c)<0)return 1;if(c=='\n' || c=='\r'){column=0;leading=1;}
            else if(c=='\b'){if(column)column--;leading=0;}else {if(column==0xFFFFFFFFu)return 1;column++;leading=0;}}
    }
    if(pending && unexpand_blanks(&out,pending-1,column,leading || all_blanks)<0)return 1;return unexpand_flush(&out)<0?1:0;
}
int main(void)
{
    if(cli_parse()<0)return 2;int at=0;for(;at<cli_argc;at++){char *option=cli_argv[at];if(equal(option,"--")){at++;break;}if(option[0]!='-' || !option[1])break;
        if(equal(option,"-a")){all_blanks=1;continue;}if(equal(option,"--first-only")){all_blanks=0;continue;}
        if(option[1]=='t'){const char *text=option+2;if(!*text){if(++at==cli_argc)return 2;text=cli_argv[at];}stop_count=0;if(tab_stops(text)<0)return 2;all_blanks=1;}else return 2;}
    return text_files(at,unexpand_run);
}
