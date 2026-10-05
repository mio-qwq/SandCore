/* mio：读取完整双字输入，普通三环执行，不拿宿主结果替代。 */
#include "SCAPI.H"
#include "SCWIDE.inc"
static volatile int wide_probe[8];
static u32 *result;
static int used=8;
static void save(ScwInt *value){result[used++]=value->low;result[used++]=(u32)value->high;}
static void vectors(int *a,int *b,u32 *entry){
    a[0]=b[0]=0x13579BDF;a[4]=b[4]=0x2468ACE0;
    for(int i=0;i<3;i++){a[i+1]=(int)entry[i];b[i+1]=(int)entry[i+3];}
}
static int guards(int *a,int *b){return a[0]!=0x13579BDF||b[0]!=0x13579BDF||a[4]!=0x2468ACE0||b[4]!=0x2468ACE0;}
static int registers(void){
    u32 b,s,d;ScwInt value;int aa[3]={-2147483647,-32768,65535},bb[3]={2147483647,65535,-65536};
    __asm__ __volatile__("mov $0x13579BDF, %%ebx; mov $0x2468ACE0, %%esi; mov $0x01234567, %%edi"
        : : : "ebx","esi","edi","cc");
    scw_dot3(&value,-2147483647,-32768,65535,2147483647,65535,-65536);
    scw_dot3v(&value,aa,bb);
    __asm__ __volatile__("mov %%ebx, %0; mov %%esi, %1; mov %%edi, %2"
        : "=a"(b),"=c"(s),"=d"(d) : : "cc");
    return b!=0x13579BDFu||s!=0x2468ACE0u||d!=0x01234567u;
}
int main(void){
    int win=sc_open("SCWIDE integers / mio",300,160);if(win<0)return 1;
    page(win,"Wide integer verification","Full values / aliases / guards");
    u32 stat[2];if(sc_stat("HOME/WIDE.IN",stat)||stat[1]!=32+4096*56)return 2;
    u8 *input=sc_alloc(stat[1]);if(!input)return 3;
    if(sc_read("HOME/WIDE.IN",input,(int)stat[1])!=(int)stat[1])return 4;
    char *magic="SWIN1MIO";for(int i=0;i<8;i++)if(input[i]!=(u8)magic[i])return 5;
    u32 *header=(u32 *)input;if(header[2]!=1||header[3]!=4096||header[4]!=14)return 6;
    int bytes=32+4096*128+64;result=sc_alloc((u32)bytes);if(!result)return 7;
    int errors=registers(),begin=sc_tick();
    for(int i=0;i<4096;i++){
        u32 *entry=(u32 *)(input+32)+i*14;ScwInt a,b,n,d,value,alias;
        a.low=entry[6];a.high=(int)entry[7];b.low=entry[8];b.high=(int)entry[9];
        n.low=entry[10];n.high=(int)entry[11];d.low=entry[12];d.high=(int)entry[13];
        scw_mul32(&value,(int)entry[0],(int)entry[1]);save(&value);
        scw_add(&value,&a,&b);save(&value);scw_sub(&value,&a,&b);save(&value);
        scw_neg(&value,&a);save(&value);
        alias=a;scw_add(&alias,&alias,&b);save(&alias);
        alias=b;scw_sub(&alias,&a,&alias);save(&alias);
        alias=a;scw_neg(&alias,&alias);save(&alias);
        scw_dot3(&value,(int)entry[0],(int)entry[1],(int)entry[2],(int)entry[3],(int)entry[4],(int)entry[5]);save(&value);
        result[used++]=(u32)scw_cmp_signed(&a,&b);result[used++]=(u32)scw_cmp_unsigned(&a,&b);
        int quotient=0;result[used++]=(u32)scw_ratio16(&quotient,&n,&d);result[used++]=(u32)quotient;
        int av[5],bv[5];vectors(av,bv,entry);
        scw_dot3v(&value,av+1,bv+1);save(&value);errors+=guards(av,bv);
        scw_dot3v((ScwInt *)(av+1),av+1,bv+1);save((ScwInt *)(av+1));
        errors+=guards(av,bv);if(av[3]!=(int)entry[2])errors++;
        vectors(av,bv,entry);
        scw_dot3v((ScwInt *)(bv+1),av+1,bv+1);save((ScwInt *)(bv+1));
        errors+=guards(av,bv);if(bv[3]!=(int)entry[5])errors++;
        vectors(av,bv,entry);
        scw_dot3v((ScwInt *)(av+2),av+1,bv+1);save((ScwInt *)(av+2));
        errors+=guards(av,bv);if(av[1]!=(int)entry[0])errors++;
        vectors(av,bv,entry);
        scw_dot3v((ScwInt *)(bv+2),av+1,bv+1);save((ScwInt *)(bv+2));
        errors+=guards(av,bv);if(bv[1]!=(int)entry[3])errors++;
        vectors(av,bv,entry);
        scw_dot3v((ScwInt *)(av+1),av+1,av+1);save((ScwInt *)(av+1));errors+=guards(av,bv);
    }
    if(used*4!=bytes-64)errors++;
    for(int i=0;i<16;i++)result[used+i]=0x534D494Fu;
    magic="SWOT1MIO";for(int i=0;i<8;i++)((u8 *)result)[i]=(u8)magic[i];
    result[2]=1;result[3]=4096;result[4]=32;
#if defined(__SCCC_WIDE__)
    result[5]=1;
#else
    result[5]=0;
#endif
    result[6]=(u32)errors;result[7]=4096*128;
    wide_probe[4]=(int)result[5];wide_probe[5]=sc_tick()-begin;
    wide_probe[1]=sc_write("HOME/WIDE.OUT",result,bytes);wide_probe[2]=errors;
    page(win,errors?"FAIL / registers":"4096 cases / complete","Esc: return");
    wide_probe[0]=1;while(sc_key()!=27)sc_yield();sc_free(result);sc_free(input);return errors;
}
