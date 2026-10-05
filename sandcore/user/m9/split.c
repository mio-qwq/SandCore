#include "../SCTEXT.H"
/* 分块正文直接写SandFS事务，不先把整个输入放进RAM。仅完整分块
 * 才commit；失败丢弃当前块，此前完整产物保留，文件名后缀不回绕。 */
static int suffix_length=2,numeric;
static char suffix[9];
static int suffix_next(void)
{char first=numeric?'0':'a',last=numeric?'9':'z';for(int i=suffix_length-1;i>=0;i--){if(suffix[i]!=last){suffix[i]++;return 0;}suffix[i]=first;}return -1;}
static int split_size(const char *text,u32 *out)
{
    u32 n=0;int digits=0;while(*text>='0' && *text<='9'){u32 d=(u32)(*text++-'0');if(n>(0xFFFFFFFFu-d)/10)return -1;n=n*10+d;digits=1;}
    u32 factor=1;if(*text){if(*text=='k'||*text=='K')factor=1024;else if(*text=='m'||*text=='M')factor=1048576;else if(*text=='g'||*text=='G')factor=1073741824;else if(*text=='b')factor=512;else return -1;text++;
        if(*text=='B')text++;if(*text)return -1;}if(!digits || !n || n>0x7FFFFFFFu/factor)return -1;*out=n*factor;return 0;
}
static int split_same(const char *a,const char *b)
{while(*a && *b){u8 x=(u8)*a++,y=(u8)*b++;if(x>='a'&&x<='z')x-=32;if(y>='a'&&y<='z')y-=32;if(x!=y)return 0;}return !*a && !*b;}
int main(void)
{
    if(cli_parse()<0)return 2;u32 limit=1000,reserve=1048576;int bytes_mode=0,at=0;
    for(;at<cli_argc;at++){char *option=cli_argv[at];if(equal(option,"--")){at++;break;}if(option[0]!='-' || !option[1])break;
        if(equal(option,"-d")){numeric=1;continue;}if(option[1]!='a' && option[1]!='b' && option[1]!='l')return 2;const char *value=option+2;if(!*value){if(++at==cli_argc)return 2;value=cli_argv[at];}
        if(option[1]=='a'){int n;if(cli_integer(value,&n)<0 || n<1 || n>8)return 2;suffix_length=n;}
        else {if(split_size(value,&limit)<0)return 2;bytes_mode=option[1]=='b';}}
    if(cli_argc-at>2)return 2;const char *input_name=at<cli_argc?cli_argv[at]:"-",*prefix=at+1<cli_argc?cli_argv[at+1]:"x";
    int prefix_length=length(prefix);if(prefix_length+suffix_length>=64)return 2;
    char input_path[64]={0},output_name[64],output_path[64],setting[256];u32 known=0,consumed=0,info[2];int finite=0;
    if(!equal(input_name,"-")){if(cli_path(input_name,input_path)<0 || sc_stat(input_path,info)<0 || info[0]!=1)return 1;known=info[1];finite=1;}
    int configured;if(sc_user(4,"REDIRECT_LIMIT",setting,256)>=0 && !cli_integer(setting,&configured) && configured>0)reserve=(u32)configured;
    int input=equal(input_name,"-")?0:sc_stream_open(input_name,1,0);if(input<0)return cli_error("split: input",input);
    for(int i=0;i<suffix_length;i++)suffix[i]=numeric?'0':'a';suffix[suffix_length]=0;
    u8 data[4096];int output=-1,result=0,exhausted=0;u32 in_chunk=0;
    for(;;){int n=cli_read(input,data,sizeof(data));if(n<0){result=cli_error("split: read",n);break;}if(!n)break;if(finite && (consumed>known || (u32)n>known-consumed)){result=cli_error("split: input changed",-1);break;}u32 start=0;
        for(u32 i=0;i<(u32)n;i++){
            if(output<0){if(exhausted){result=cli_error("split: suffix exhausted",-1);goto done;}copy(output_name,prefix,64);append(output_name,suffix,64);
                if(cli_path(output_name,output_path)<0 || (finite && split_same(input_path,output_path))){result=cli_error("split: output aliases input",-1);goto done;}
                u32 capacity=bytes_mode?limit:reserve;if(finite && known-consumed<capacity)capacity=known-consumed;
                output=sc_stream_open(output_name,2,capacity);if(output<0){result=cli_error("split: output",output);goto done;}in_chunk=0;start=i;}
            consumed++;if(bytes_mode || data[i]=='\n')in_chunk++;
            if(in_chunk==limit){if(cli_write(output,data+start,i+1-start)<0){result=1;goto done;}int commit=sc_stream_close(output,1);if(commit<0){result=cli_error("split: commit",commit);goto done;}output=-1;
                exhausted=suffix_next()<0;start=i+1;}
        }
        if(output>=0 && start<(u32)n && cli_write(output,data+start,(u32)n-start)<0){result=cli_error("split: write",-1);break;}
    }
    if(!result && output>=0){int commit=sc_stream_close(output,1);if(commit<0)result=cli_error("split: commit",commit);else output=-1;}
done:
    if(output>=0)sc_stream_close(output,0);if(input)sc_stream_close(input,0);return result;
}
