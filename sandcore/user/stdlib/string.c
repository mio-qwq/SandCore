#include "SCCLIBPRIVATE.H"
void *memcpy(void *dest,const void *source,size_t count)
{
    unsigned char *out=dest;
    const unsigned char *in=source;
    /* 只按明确长度访问，末尾不足一块绝不读出对象。 */
    while(count>=8){
        out[0]=in[0];
        out[1]=in[1];
        out[2]=in[2];
        out[3]=in[3];
        out[4]=in[4];
        out[5]=in[5];
        out[6]=in[6];
        out[7]=in[7];
        out+=8;
        in+=8;
        count-=8;
    }
    while(count--)*out++=*in++;
    return dest;
}
void *memmove(void *dest,const void *source,size_t count)
{
    unsigned char *out=dest;
    const unsigned char *in=source;
    /* 固定i386地址关系用于选择方向，不做跨对象C有序指针比较。 */
    if((uintptr_t)out>(uintptr_t)in && (uintptr_t)out-(uintptr_t)in<count){
        out+=count;
        in+=count;
        while(count--)*--out=*--in;
    }
    else memcpy(dest,source,count);
    return dest;
}
void *memset(void *dest,int value,size_t count)
{
    unsigned char *out=dest;
    unsigned char byte=(unsigned char)value;
    while(count>=8){
        out[0]=byte;
        out[1]=byte;
        out[2]=byte;
        out[3]=byte;
        out[4]=byte;
        out[5]=byte;
        out[6]=byte;
        out[7]=byte;
        out+=8;
        count-=8;
    }
    while(count--)*out++=byte;
    return dest;
}
int memcmp(const void *left,const void *right,size_t count)
{
    const unsigned char *a=left,*b=right;
    while(count--){
        if(*a!=*b)return (int)*a-(int)*b;
        a++;
        b++;
    }
    return 0;
}
void *memchr(const void *data,int value,size_t count)
{
    const unsigned char *in=data;
    unsigned char byte=(unsigned char)value;
    while(count--){
        if(*in==byte)return (void *)in;
        in++;
    }
    return NULL;
}
size_t strlen(const char *text)
{
    const char *end=text;
    while(*end)end++;
    return (size_t)(end-text);
}
char *strcpy(char *dest,const char *source)
{
    char *out=dest;
    do{
        *out++=*source;
    }
    while(*source++);
    return dest;
}
char *strncpy(char *dest,const char *source,size_t count)
{
    char *out=dest;
    while(count && *source){
        *out++=*source++;
        count--;
    }
    while(count--)*out++=0;
    return dest;
}
char *strcat(char *dest,const char *source)
{
    strcpy(dest+strlen(dest),source);
    return dest;
}
char *strncat(char *dest,const char *source,size_t count)
{
    char *out=dest+strlen(dest);
    while(count && *source){
        *out++=*source++;
        count--;
    }
    *out=0;
    return dest;
}
int strcmp(const char *left,const char *right)
{
    while(*left && *left==*right){
        left++;
        right++;
    }
    return (int)(unsigned char)*left-(int)(unsigned char)*right;
}
int strncmp(const char *left,const char *right,size_t count)
{
    while(count--){
        unsigned int a=(unsigned char)*left++,b=(unsigned char)*right++;
        if(a!=b)return (int)a-(int)b;
        if(!a)break;
    }
    return 0;
}
char *strchr(const char *text,int value)
{
    char target=(char)value;
    for(;;){
        if(*text==target)return (char *)text;
        if(!*text)return NULL;
        text++;
    }
}
char *strrchr(const char *text,int value)
{
    char target=(char)value;
    const char *found=NULL;
    do{
        if(*text==target)found=text;
    }
    while(*text++);
    return (char *)found;
}
static void __scc_byte_set(unsigned char bits[32],const char *text)
{
    memset(bits,0,32);
    while(*text){
        unsigned int byte=(unsigned char)*text++;
        bits[byte>>3]|=(unsigned char)(1u<<(byte&7));
    }
}
static int __scc_byte_has(const unsigned char bits[32],unsigned int byte)
{
    return (bits[byte>>3]&(1u<<(byte&7)))!=0;
}
size_t strspn(const char *text,const char *accept)
{
    unsigned char bits[32];
    __scc_byte_set(bits,accept);
    size_t count=0;
    while(text[count] && __scc_byte_has(bits,(unsigned char)text[count]))count++;
    return count;
}
size_t strcspn(const char *text,const char *reject)
{
    unsigned char bits[32];
    __scc_byte_set(bits,reject);
    size_t count=0;
    while(text[count] && !__scc_byte_has(bits,(unsigned char)text[count]))count++;
    return count;
}
char *strpbrk(const char *text,const char *accept)
{
    unsigned char bits[32];
    __scc_byte_set(bits,accept);
    while(*text){
        if(__scc_byte_has(bits,(unsigned char)*text))return (char *)text;
        text++;
    }
    return NULL;
}
static char *__scc_token_position;
char *strtok(char *text,const char *delimiters)
{
    if(!text)text=__scc_token_position;
    if(!text)return NULL;
    unsigned char bits[32];
    __scc_byte_set(bits,delimiters);
    while(*text && __scc_byte_has(bits,(unsigned char)*text))text++;
    if(!*text){
        __scc_token_position=NULL;
        return NULL;
    }
    char *begin=text;
    while(*text && !__scc_byte_has(bits,(unsigned char)*text))text++;
    if(*text){
        *text++=0;
        __scc_token_position=text;
    }
    else __scc_token_position=NULL;
    return begin;
}
static size_t __scc_maximal_suffix(const unsigned char *needle,size_t length,int reverse,size_t *period)
{
    /* ms的SIZE_MAX表示概念下标-1；与k相加使用定义明确的unsigned回绕。 */
    size_t ms=SIZE_MAX,j=0,k=1,p=1;
    while(k<length-j){
        unsigned int a=needle[j+k],b=needle[ms+k];
        if(a==b){
            if(k==p){
                j+=p;
                k=1;
            }
            else k++;
        }
        else if(reverse?a>b:a<b){
            j+=k;
            k=1;
            p=j-ms;
        }
        else{
            ms=j;
            j++;
            k=p=1;
        }
    }
    *period=p;
    return ms+1u;
}
char *strstr(const char *text,const char *search)
{
    size_t length=strlen(search);
    if(!length)return (char *)text;
    if(length==1)return strchr(text,(unsigned char)search[0]);
    size_t available=strlen(text);
    if(length>available)return NULL;
    const unsigned char *needle=(const unsigned char *)search,*hay=(const unsigned char *)text;
    size_t p1,p2,cut1=__scc_maximal_suffix(needle,length,0,&p1);
    size_t cut2=__scc_maximal_suffix(needle,length,1,&p2);
    size_t cut=cut1>cut2?cut1:cut2,period=cut1>cut2?p1:p2;
    int periodic=period<=length-cut && !memcmp(needle,needle+period,cut);
    if(!periodic)period=cut>length-cut?cut:length-cut+1u;
    size_t position=0,memory=0;
    /* 先比较临界分解右半；失配按证实的周期前进，长needle不退为平方扫描。 */
    while(position<=available-length){
        size_t k=cut>memory?cut:memory;
        while(k<length && needle[k]==hay[position+k])k++;
        if(k<length){
            position+=k-cut+1u;
            memory=0;
            continue;
        }
        k=cut;
        while(k>memory && needle[k-1u]==hay[position+k-1u])k--;
        if(k<=memory)return (char *)(text+position);
        position+=period;
        memory=periodic?length-period:0;
    }
    return NULL;
}
int strcoll(const char *left,const char *right) {
    return strcmp(left,right);
}
size_t strxfrm(char *dest,const char *source,size_t count)
{
    size_t length=strlen(source);
    if(length<count)memcpy(dest,source,length+1u);
    return length;
}
