#include "../SCTEXT.H"
/* 整数RPN计算器；栈和寄存器有界，无浮点、宏执行或隐式溢出截断。
 * 32位二补码加减乘与Shell整数一致，除零/栈不足明确失败。 */
static int stack[256],registers[128],register_set[128],top,base=10,failed,quit;
static int push(int value){if(top==256)return -1;stack[top++]=value;return 0;}
static int pop(int *out){if(!top)return -1;*out=stack[--top];return 0;}
static int print_value(int value)
{char out[36];const char *digits="0123456789ABCDEF";int used=0,minus=value<0;u32 n=minus?0u-(u32)value:(u32)value;
    do{out[used++]=digits[n%(u32)base];n/=(u32)base;}while(n);if(minus)out[used++]='-';for(int i=0;i<used/2;i++){char c=out[i];out[i]=out[used-1-i];out[used-1-i]=c;}out[used++]='\n';return cli_write(1,out,(u32)used)<0?-1:0;}
static int evaluate(const char *text,u32 bytes)
{
    for(u32 at=0;at<bytes && !quit;at++){char c=text[at];if(c==' '||c=='\t'||c=='\r'||c=='\n')continue;if(c=='#'){while(at<bytes && text[at]!='\n')at++;continue;}
        if((c>='0'&&c<='9') || c=='_'){int minus=c=='_';if(minus && ++at==bytes)return -1;u32 n=0,count=0;
            while(at<bytes && text[at]>='0'&&text[at]<='9'){u32 d=(u32)(text[at++]-'0'),limit=minus?0x80000000u:0x7FFFFFFFu;if(n>(limit-d)/10)return -1;n=n*10+d;count++;}if(!count || push(minus?(int)(0u-n):(int)n)<0)return -1;at--;continue;}
        int a,b;if(c=='p'){if(!top || print_value(stack[top-1])<0)return -1;}
        else if(c=='f'){for(int i=top-1;i>=0;i--)if(print_value(stack[i])<0)return -1;}
        else if(c=='c')top=0;else if(c=='d'){if(!top || push(stack[top-1])<0)return -1;}else if(c=='r'){if(top<2)return -1;a=stack[top-1];stack[top-1]=stack[top-2];stack[top-2]=a;}
        else if(c=='z'){if(push(top)<0)return -1;}else if(c=='q')quit=1;
        else if(c=='o'){if(pop(&a)<0 || a<2 || a>16)return -1;base=a;}
        else if(c=='s'||c=='l'){if(++at==bytes || (u8)text[at]>=128)return -1;u8 index=(u8)text[at];if(c=='s'){if(pop(&registers[index])<0)return -1;register_set[index]=1;}else if(!register_set[index] || push(registers[index])<0)return -1;}
        else if(c=='+'||c=='-'||c=='*'||c=='/'||c=='%'){if(top<2)return -1;b=stack[top-1];a=stack[top-2];if((c=='/'||c=='%') && !b)return -1;
            int value=c=='+'?(int)((u32)a+(u32)b):c=='-'?(int)((u32)a-(u32)b):c=='*'?(int)((u32)a*(u32)b):a==(int)0x80000000u && b==-1?(c=='/'?a:0):c=='/'?a/b:a%b;top-=2;push(value);}
        else return -1;}return 0;
}
int main(void)
{
    if(cli_parse()<0)return 2;int at=0;for(;at<cli_argc;at++){if(equal(cli_argv[at],"-e")){if(++at==cli_argc || evaluate(cli_argv[at],(u32)length(cli_argv[at]))<0)return 2;if(quit)return 0;}
        else if(equal(cli_argv[at],"--")){at++;break;}else break;}
    if(at==cli_argc && at)return 0;for(int file=at;file<cli_argc || (file==at && at==cli_argc);file++){int fd=cli_input(file);if(fd<0)return 1;text_reader r={fd,{0},0,0};text_line line={0};int n=0;
        while(!quit && (n=text_line_read(&r,&line))>0)if(evaluate(line.bytes,line.size)<0){failed=1;break;}if(n<0)failed=1;text_line_free(&line);if(fd)sc_stream_close(fd,0);if(failed || quit)break;}
    if(failed)cli_text(2,"dc: invalid expression/stack/read\n");return failed;
}
