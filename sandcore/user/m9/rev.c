#include "../SCTEXT.H"
static int run(int fd,const char *name)
{(void)name;text_reader r={fd,{0},0,0};text_line l={0,0,0,0};int n,result=0;
    while((n=text_line_read(&r,&l))>0){
        /* 先逆序字节，再把每个逆序UTF-8编码单元恢复，保留汉字。
         * 这按Unicode码点反转，不宣称把组合字素当一个字符。 */
        for(u32 i=0;i<l.size/2;i++){char c=l.bytes[i];l.bytes[i]=l.bytes[l.size-1-i];l.bytes[l.size-1-i]=c;}
        for(u32 i=0;i<l.size;){u32 start=i;while(i<l.size && ((u8)l.bytes[i]&0xC0)==0x80)i++;if(i<l.size)i++;
            for(u32 a=start,b=i;i-start>1 && a<--b;a++){char c=l.bytes[a];l.bytes[a]=l.bytes[b];l.bytes[b]=c;}}
        if(text_line_write(1,&l)<0){result=1;break;}}
    text_line_free(&l);return result || n<0;}
int main(void){if(cli_parse()<0)return 2;return text_files(0,run);}
