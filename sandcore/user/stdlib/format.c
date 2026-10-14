#include "SCCLIBPRIVATE.H"
#define SCC_FMT_LEFT 1u
#define SCC_FMT_PLUS 2u
#define SCC_FMT_SPACE 4u
#define SCC_FMT_ALT 8u
#define SCC_FMT_ZERO 16u
enum {
    SCC_LEN_NONE,SCC_LEN_HH,SCC_LEN_H,SCC_LEN_L,SCC_LEN_Z,SCC_LEN_T
};
typedef struct {
    unsigned int flags;
    int width,precision,width_argument,precision_argument,length,conversion;
}
__scc_format_spec;
typedef struct {
    FILE *stream;
    char *buffer;
    size_t capacity,stored;
    unsigned int total;
    int failed;
}
__scc_format_sink;
static int __scc_format_decimal(const char **format,int *result)
{
    unsigned int value=0;
    while(**format>='0' && **format<='9'){
        unsigned int digit=(unsigned int)(**format-'0');
        if(value>((unsigned int)INT_MAX-digit)/10u){
            errno=EOVERFLOW;
            return -1;
        }
        value=value*10u+digit;
        (*format)++;
    }
    *result=(int)value;
    return 0;
}
static int __scc_format_parse(const char **format,__scc_format_spec *spec)
{
    const char *p=*format;
    memset(spec,0,sizeof(*spec));
    spec->precision=-1;
    for(;;){
        if(*p=='-')spec->flags|=SCC_FMT_LEFT;
        else if(*p=='+')spec->flags|=SCC_FMT_PLUS;
        else if(*p==' ')spec->flags|=SCC_FMT_SPACE;
        else if(*p=='#')spec->flags|=SCC_FMT_ALT;
        else if(*p=='0')spec->flags|=SCC_FMT_ZERO;
        else break;
        p++;
    }
    if(*p=='*'){
        spec->width_argument=1;
        p++;
    }
    else if(__scc_format_decimal(&p,&spec->width))return -1;
    if(*p=='.'){
        p++;
        spec->precision=0;
        if(*p=='*'){
            spec->precision_argument=1;
            p++;
        }
        else if(__scc_format_decimal(&p,&spec->precision))return -1;
    }
    if(*p=='h'){
        p++;
        spec->length=SCC_LEN_H;
        if(*p=='h'){
            p++;
            spec->length=SCC_LEN_HH;
        }
    }
    else if(*p=='l'){
        p++;
        spec->length=SCC_LEN_L;
        if(*p=='l'){
            errno=ENOSYS;
            return -1;
        }
    }
    else if(*p=='z'){
        p++;
        spec->length=SCC_LEN_Z;
    }
    else if(*p=='t'){
        p++;
        spec->length=SCC_LEN_T;
    }
    else if(*p=='j' || *p=='L'){
        errno=ENOSYS;
        return -1;
    }
    if(!*p){
        errno=EINVAL;
        return -1;
    }
    spec->conversion=(unsigned char)*p++;
    if(strchr("fFeEgGaA",spec->conversion)){
        errno=ENOSYS;
        return -1;
    }
    if(!strchr("diuoxXcspn%",spec->conversion)){
        errno=EINVAL;
        return -1;
    }
    if((spec->conversion=='c' || spec->conversion=='s' || spec->conversion=='p' || spec->conversion=='%') && spec->length){
        errno=ENOSYS;
        return -1;
    }
    if(spec->conversion=='n' && (spec->flags || spec->width || spec->width_argument || spec->precision>=0 || spec->precision_argument)){
        errno=EINVAL;
        return -1;
    }
    if(spec->conversion=='%' && (spec->flags || spec->width || spec->width_argument || spec->precision>=0 || spec->precision_argument)){
        errno=EINVAL;
        return -1;
    }
    if(spec->conversion=='c' && (spec->precision>=0 || spec->precision_argument)){
        errno=EINVAL;
        return -1;
    }
    *format=p;
    return 0;
}
static int __scc_format_check(const char *format)
{
    if(!format){
        errno=EINVAL;
        return -1;
    }
    while(*format){
        if(*format++!='%')continue;
        __scc_format_spec spec;
        if(__scc_format_parse(&format,&spec))return -1;
    }
    return 0;
}
static void __scc_format_bytes(__scc_format_sink *sink,const char *bytes,size_t count)
{
    if(sink->failed || !count)return;
    if(count>(unsigned int)INT_MAX-sink->total){
        errno=EOVERFLOW;
        sink->failed=1;
        return;
    }
    if(sink->stream){
        size_t accepted=fwrite(bytes,1,count,sink->stream);
        sink->total+=(unsigned int)accepted;
        if(accepted!=count || __scc_stdio_failed(sink->stream))sink->failed=1;
    }
    else{
        size_t available=sink->capacity && sink->stored<sink->capacity-1u?sink->capacity-1u-sink->stored:0;
        size_t copy=count<available?count:available;
        if(copy){
            memcpy(sink->buffer+sink->stored,bytes,copy);
            sink->stored+=copy;
        }
        sink->total+=(unsigned int)count;
    }
}
static void __scc_format_repeat(__scc_format_sink *sink,char value,unsigned int count)
{
    if(sink->failed || !count)return;
    if(count>(unsigned int)INT_MAX-sink->total){
        errno=EOVERFLOW;
        sink->failed=1;
        return;
    }
    if(!sink->stream){
        size_t available=sink->capacity && sink->stored<sink->capacity-1u?sink->capacity-1u-sink->stored:0;
        size_t copy=count<available?count:available;
        if(copy){
            memset(sink->buffer+sink->stored,(unsigned char)value,copy);
            sink->stored+=copy;
        }
        sink->total+=count;
        return;
    }
    char block[64];
    memset(block,(unsigned char)value,sizeof(block));
    while(count && !sink->failed){
        unsigned int portion=count<sizeof(block)?count:sizeof(block);
        __scc_format_bytes(sink,block,portion);
        count-=portion;
    }
}
static void __scc_format_number(__scc_format_sink *sink,__scc_format_spec *spec,unsigned long number,int negative,int signed_value,int pointer)
{
    char reversed[32],digits[32],prefix[3];
    unsigned int length=0,prefix_length=0;
    unsigned int radix=spec->conversion=='o'?8u:spec->conversion=='x' || spec->conversion=='X' || pointer?16u:10u;
    const char *alphabet=spec->conversion=='X'?"0123456789ABCDEF":"0123456789abcdef";
    unsigned long original=number;
    if(number || spec->precision!=0 || pointer){
        do{
            reversed[length++]=alphabet[number%radix];
            number/=radix;
        }
        while(number);
    }
    for(unsigned int i=0;i<length;i++)digits[i]=reversed[length-i-1u];
    if(signed_value){
        if(negative)prefix[prefix_length++]='-';
        else if(spec->flags&SCC_FMT_PLUS)prefix[prefix_length++]='+';
        else if(spec->flags&SCC_FMT_SPACE)prefix[prefix_length++]=' ';
    }
    if(pointer || ((spec->flags&SCC_FMT_ALT) && radix==16u && original)){
        prefix[prefix_length++]='0';
        prefix[prefix_length++]=spec->conversion=='X'?'X':'x';
    }
    unsigned int precision=pointer?8u:spec->precision<0?0u:(unsigned int)spec->precision;
    unsigned int zeros=precision>length?precision-length:0;
    if((spec->flags&SCC_FMT_ALT) && radix==8u && (!length || (!zeros && digits[0]!='0')))zeros++;
    unsigned int content=prefix_length+length;
    if(zeros>(unsigned int)INT_MAX-content){
        errno=EOVERFLOW;
        sink->failed=1;
        return;
    }
    content+=zeros;
    unsigned int padding=(unsigned int)spec->width>content?(unsigned int)spec->width-content:0;
    if((spec->flags&SCC_FMT_ZERO) && !(spec->flags&SCC_FMT_LEFT) && spec->precision<0){
        zeros+=padding;
        padding=0;
    }
    if(!(spec->flags&SCC_FMT_LEFT))__scc_format_repeat(sink,' ',padding);
    __scc_format_bytes(sink,prefix,prefix_length);
    __scc_format_repeat(sink,'0',zeros);
    __scc_format_bytes(sink,digits,length);
    if(spec->flags&SCC_FMT_LEFT)__scc_format_repeat(sink,' ',padding);
}
static int __scc_format_run(__scc_format_sink *sink,const char *format,va_list arguments)
{
    if(__scc_format_check(format))return -1;
    va_list ap;
    va_copy(ap,arguments);
    while(*format && !sink->failed){
        const char *literal=format;
        while(*format && *format!='%')format++;
        __scc_format_bytes(sink,literal,(size_t)(format-literal));
        if(!*format || sink->failed)break;
        format++;
        __scc_format_spec spec;
        if(__scc_format_parse(&format,&spec)){
            sink->failed=1;
            break;
        }
        if(spec.width_argument){
            int width=va_arg(ap,int);
            if(width<0){
                unsigned int magnitude=0u-(unsigned int)width;
                if(magnitude>(unsigned int)INT_MAX){
                    errno=EOVERFLOW;
                    sink->failed=1;
                    break;
                }
                spec.width=(int)magnitude;
                spec.flags|=SCC_FMT_LEFT;
            }
            else spec.width=width;
        }
        if(spec.precision_argument){
            int precision=va_arg(ap,int);
            spec.precision=precision<0?-1:precision;
        }
        int conversion=spec.conversion;
        if(conversion=='%'){
            __scc_format_bytes(sink,"%",1);
            continue;
        }
        if(conversion=='s' || conversion=='c'){
            char byte;
            const char *text;
            size_t length=0;
            if(conversion=='c'){
                byte=(char)(unsigned char)va_arg(ap,int);
                text=&byte;
                length=1;
            }
            else{
                text=va_arg(ap,char *);
                if(!text){
                    errno=EINVAL;
                    sink->failed=1;
                    break;
                }
                if(spec.precision<0)length=strlen(text);
                else while(length<(unsigned int)spec.precision && text[length])length++;
            }
            unsigned int padding=(unsigned int)spec.width>length?(unsigned int)spec.width-length:0;
            if(!(spec.flags&SCC_FMT_LEFT))__scc_format_repeat(sink,' ',padding);
            __scc_format_bytes(sink,text,length);
            if(spec.flags&SCC_FMT_LEFT)__scc_format_repeat(sink,' ',padding);
        }
        else if(conversion=='n'){
            if(spec.length==SCC_LEN_HH)*va_arg(ap,signed char *)=(signed char)sink->total;
            else if(spec.length==SCC_LEN_H)*va_arg(ap,short *)=(short)sink->total;
            else if(spec.length==SCC_LEN_L)*va_arg(ap,long *)=(long)sink->total;
            else *va_arg(ap,int *)=(int)sink->total;
        }
        else if(conversion=='p'){
            void *pointer=va_arg(ap,void *);
            __scc_format_number(sink,&spec,(uintptr_t)pointer,0,0,1);
        }
        else if(conversion=='d' || conversion=='i'){
            long value;
            if(spec.length==SCC_LEN_L)value=va_arg(ap,long);
            else value=va_arg(ap,int);
            if(spec.length==SCC_LEN_HH)value=(signed char)value;
            else if(spec.length==SCC_LEN_H)value=(short)value;
            int negative=value<0;
            unsigned long number=negative?0UL-(unsigned long)value:(unsigned long)value;
            __scc_format_number(sink,&spec,number,negative,1,0);
        }
        else{
            unsigned long number;
            if(spec.length==SCC_LEN_L)number=va_arg(ap,unsigned long);
            else if(spec.length==SCC_LEN_HH || spec.length==SCC_LEN_H)number=(unsigned int)va_arg(ap,int);
            else number=va_arg(ap,unsigned int);
            if(spec.length==SCC_LEN_HH)number=(unsigned char)number;
            else if(spec.length==SCC_LEN_H)number=(unsigned short)number;
            __scc_format_number(sink,&spec,number,0,0,0);
        }
    }
    va_end(ap);
    return sink->failed?-1:(int)sink->total;
}
int vfprintf(FILE *stream,const char *format,va_list arguments)
{
    if(!stream){
        errno=EBADF;
        return -1;
    }
    __scc_format_sink sink;
    memset(&sink,0,sizeof(sink));
    sink.stream=stream;
    return __scc_format_run(&sink,format,arguments);
}
int vprintf(const char *format,va_list arguments) {
    return vfprintf(stdout,format,arguments);
}
int vsnprintf(char *data,size_t capacity,const char *format,va_list arguments)
{
    if(capacity && !data){
        errno=EINVAL;
        return -1;
    }
    __scc_format_sink sink;
    memset(&sink,0,sizeof(sink));
    sink.buffer=data;
    sink.capacity=capacity;
    int result=__scc_format_run(&sink,format,arguments);
    if(capacity)data[sink.stored]=0;
    return result;
}
int vsprintf(char *data,const char *format,va_list arguments)
{
    if(!data){
        errno=EINVAL;
        return -1;
    }
    return vsnprintf(data,SIZE_MAX,format,arguments);
}
int fprintf(FILE *stream,const char *format,...)
{
    va_list ap;
    va_start(ap,format);
    int result=vfprintf(stream,format,ap);
    va_end(ap);
    return result;
}
int printf(const char *format,...)
{
    va_list ap;
    va_start(ap,format);
    int result=vfprintf(stdout,format,ap);
    va_end(ap);
    return result;
}
int snprintf(char *data,size_t capacity,const char *format,...)
{
    va_list ap;
    va_start(ap,format);
    int result=vsnprintf(data,capacity,format,ap);
    va_end(ap);
    return result;
}
int sprintf(char *data,const char *format,...)
{
    va_list ap;
    va_start(ap,format);
    int result=vsprintf(data,format,ap);
    va_end(ap);
    return result;
}
