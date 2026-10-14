#include "SCCLIBPRIVATE.H"
static int __scc_digit(unsigned int value)
{
    if(value>='0' && value<='9')return (int)(value-'0');
    if(value>='a' && value<='z')return (int)(value-'a'+10u);
    if(value>='A' && value<='Z')return (int)(value-'A'+10u);
    return -1;
}
static unsigned long __scc_integer(const char *text,char **end,int base,int signed_result,int *negative,int *overflow)
{
    const char *original=text;
    *negative=*overflow=0;
    if(base && (base<2 || base>36)){
        if(end)*end=(char *)original;
        errno=EINVAL;
        return 0;
    }
    while(isspace((unsigned char)*text))text++;
    if(*text=='-' || *text=='+'){
        *negative=*text=='-';
        text++;
    }
    /* 没有合法下一位就不吞0x；0x结尾仍能转换前面的0，end准确停在x。 */
    if((base==0 || base==16) && text[0]=='0' && (text[1]=='x' || text[1]=='X')){
        int digit=__scc_digit((unsigned char)text[2]);
        if(digit>=0 && digit<16){
            text+=2;
            base=16;
        }
    }
    if(!base)base=*text=='0'?8:10;
    unsigned long limit=signed_result?(*negative?2147483648UL:2147483647UL):ULONG_MAX;
    unsigned long cutoff=limit/(unsigned int)base,cutlim=limit%(unsigned int)base,value=0;
    const char *digits=text;
    for(;;){
        int digit=__scc_digit((unsigned char)*text);
        if(digit<0 || digit>=base)break;
        if(value>cutoff || (value==cutoff && (unsigned int)digit>cutlim))*overflow=1;
        if(!*overflow)value=value*(unsigned int)base+(unsigned int)digit;
        text++;
    }
    if(end)*end=(char *)(text==digits?original:text);
    if(text==digits)return 0;
    if(*overflow){
        errno=ERANGE;
        return limit;
    }
    return value;
}
long strtol(const char *text,char **end,int base)
{
    int negative,overflow;
    unsigned long value=__scc_integer(text,end,base,1,&negative,&overflow);
    if(overflow)return negative?LONG_MIN:LONG_MAX;
    if(negative){
        if(value==2147483648UL)return LONG_MIN;
        return -(long)value;
    }
    return (long)value;
}
unsigned long strtoul(const char *text,char **end,int base)
{
    int negative,overflow;
    unsigned long value=__scc_integer(text,end,base,0,&negative,&overflow);
    if(overflow)return ULONG_MAX;
    return negative?0UL-value:value;
}
int atoi(const char *text) {
    return (int)strtol(text,NULL,10);
}
long atol(const char *text) {
    return strtol(text,NULL,10);
}
int abs(int value) {
    return value<0?-value:value;
}
long labs(long value) {
    return value<0?-value:value;
}
static unsigned int __scc_random_state=1;
void srand(unsigned int seed) {
    __scc_random_state=seed;
}
int rand(void)
{
    /* 本进程可重复序列，只用于普通应用随机；不声称密码学质量。 */
    __scc_random_state=__scc_random_state*1664525u+1013904223u;
    return (int)((__scc_random_state>>16)&32767u);
}
static char *__scc_environment_buffer;
static size_t __scc_environment_capacity;
char *getenv(const char *name)
{
    if(!name || !*name || strchr(name,'=')){
        errno=EINVAL;
        return NULL;
    }
    /* 当前公开环境值有界；增长复用一个进程私有结果，不返回栈地址。 */
    if(__scc_environment_capacity<256u){
        char *buffer=realloc(__scc_environment_buffer,256u);
        if(!buffer)return NULL;
        __scc_environment_buffer=buffer;
        __scc_environment_capacity=256u;
    }
    unsigned int length=0;
    ScStatus status=ScProcessEnvironment(name,__scc_environment_buffer,__scc_environment_capacity,&length);
    if(status==SC32_E_NOT_FOUND)return NULL;
    if(status){
        __scc_from_status(status);
        return NULL;
    }
    if(length>=__scc_environment_capacity){
        errno=EOVERFLOW;
        return NULL;
    }
    __scc_environment_buffer[length]=0;
    return __scc_environment_buffer;
}
static void __scc_environment_finish(void)
{
    free(__scc_environment_buffer);
    __scc_environment_buffer=NULL;
    __scc_environment_capacity=0;
}
