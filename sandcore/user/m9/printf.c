#include "../SCTEXT.H"
static int emit_escape(const char **cursor,int fd)
{
    const char *p=*cursor;u8 c=(u8)*p++;if(c=='n')c='\n';else if(c=='r')c='\r';else if(c=='t')c='\t';else if(c=='b')c='\b';else if(c=='a')c=7;else if(c=='v')c=11;else if(c=='f')c=12;
    else if(c=='c'){*cursor=p;return 1;}
    else if(c=='x'){u32 n=0,used=0;while(used<2){char q=*p;int d=q>='0'&&q<='9'?q-'0':q>='a'&&q<='f'?q-'a'+10:q>='A'&&q<='F'?q-'A'+10:-1;if(d<0)break;n=n*16+(u32)d;p++;used++;}if(!used)return -1;c=(u8)n;}
    else if(c>='0' && c<='7'){u32 n=(u32)(c-'0');for(int i=1;i<3 && *p>='0'&&*p<='7';i++)n=n*8+(u32)(*p++-'0');c=(u8)n;}
    else if(c!='\\' && c!='"' && c!='\'')return -1;*cursor=p;return cli_write(fd,&c,1)<0?-1:0;
}
int main(void)
{
    if(cli_parse()<1)return 2;int argument=1;const char *format=cli_argv[0];
    do {const char *p=format;int conversions=0;
        while(*p){u8 c=(u8)*p++;if(c=='\\'){int r=emit_escape(&p,1);if(r)return r<0?2:0;continue;}if(c!='%'){if(cli_write(1,&c,1)<0)return 1;continue;}
            if(*p=='%'){p++;if(cli_text(1,"%")<0)return 1;continue;}
            int left=0,zero=0;if(*p=='-'){left=1;p++;}if(*p=='0'){zero=1;p++;}u32 width=0;
            while(*p>='0'&&*p<='9'){width=width*10+(u32)(*p++-'0');if(width>4096)return 2;}
            int precision=-1;if(*p=='.'){p++;precision=0;while(*p>='0'&&*p<='9'){precision=precision*10+*p++-'0';if(precision>4096)return 2;}}
            char type=*p++;if(!type)return 2;const char *value=argument<cli_argc?cli_argv[argument++]:"";conversions++;
            char number[36];const char *output=value;u32 n=0;
            if(type=='b'){while(*value){if(*value=='\\'){value++;int r=emit_escape(&value,1);if(r)return r<0?2:0;}else if(cli_write(1,value++,1)<0)return 1;}continue;}
            if(type=='s'){n=(u32)length(value);if(precision>=0 && n>(u32)precision)n=(u32)precision;}
            else if(type=='c'){number[0]=*value;output=number;n=1;}
            else if(type=='d'||type=='i'||type=='u'||type=='x'||type=='X'||type=='o'){
                int signed_value=0;if(*value && cli_integer(value,&signed_value)<0)return 2;u32 v=(u32)signed_value,base=type=='x'||type=='X'?16:type=='o'?8:10;
                int negative=(type=='d'||type=='i') && signed_value<0;if(negative)v=0u-v;const char *digits=type=='X'?"0123456789ABCDEF":"0123456789abcdef";
                do{number[n++]=digits[v%base];v/=base;}while(v);if(negative)number[n++]='-';for(u32 i=0;i<n/2;i++){char swap=number[i];number[i]=number[n-1-i];number[n-1-i]=swap;}output=number;
            }else return 2;
            if(!left && zero && n && output[0]=='-' && width>n){if(cli_text(1,"-")<0)return 1;output++;n--;width--;}
            if(!left)for(u32 i=n;i<width;i++)if(cli_text(1,zero?"0":" ")<0)return 1;
            if(cli_write(1,output,n)<0)return 1;if(left)for(u32 i=n;i<width;i++)if(cli_text(1," ")<0)return 1;
        }
        if(!conversions)break;
    }while(argument<cli_argc);return 0;
}
