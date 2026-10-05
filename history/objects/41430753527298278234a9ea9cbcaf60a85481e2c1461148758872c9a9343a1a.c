/* mio：普通三环数值夹具，输出显式记录实际机器码运行结果。 */
#include "SCAPI.H"
static int sd(int n,int shift){switch(shift){
case 0: return n/1;
case 1: return n/2;
case 2: return n/4;
case 3: return n/8;
case 4: return n/16;
case 5: return n/32;
case 6: return n/64;
case 7: return n/128;
case 8: return n/256;
case 9: return n/512;
case 10: return n/1024;
case 11: return n/2048;
case 12: return n/4096;
case 13: return n/8192;
case 14: return n/16384;
case 15: return n/32768;
case 16: return n/65536;
case 17: return n/131072;
case 18: return n/262144;
case 19: return n/524288;
case 20: return n/1048576;
case 21: return n/2097152;
case 22: return n/4194304;
case 23: return n/8388608;
case 24: return n/16777216;
case 25: return n/33554432;
case 26: return n/67108864;
case 27: return n/134217728;
case 28: return n/268435456;
case 29: return n/536870912;
case 30: return n/1073741824;
}return 0;}
static int sm(int n,int shift){switch(shift){
case 0: return n%1;
case 1: return n%2;
case 2: return n%4;
case 3: return n%8;
case 4: return n%16;
case 5: return n%32;
case 6: return n%64;
case 7: return n%128;
case 8: return n%256;
case 9: return n%512;
case 10: return n%1024;
case 11: return n%2048;
case 12: return n%4096;
case 13: return n%8192;
case 14: return n%16384;
case 15: return n%32768;
case 16: return n%65536;
case 17: return n%131072;
case 18: return n%262144;
case 19: return n%524288;
case 20: return n%1048576;
case 21: return n%2097152;
case 22: return n%4194304;
case 23: return n%8388608;
case 24: return n%16777216;
case 25: return n%33554432;
case 26: return n%67108864;
case 27: return n%134217728;
case 28: return n%268435456;
case 29: return n%536870912;
case 30: return n%1073741824;
}return 0;}
static u32 ud(u32 n,int shift){switch(shift){
case 0: return n/1u;
case 1: return n/2u;
case 2: return n/4u;
case 3: return n/8u;
case 4: return n/16u;
case 5: return n/32u;
case 6: return n/64u;
case 7: return n/128u;
case 8: return n/256u;
case 9: return n/512u;
case 10: return n/1024u;
case 11: return n/2048u;
case 12: return n/4096u;
case 13: return n/8192u;
case 14: return n/16384u;
case 15: return n/32768u;
case 16: return n/65536u;
case 17: return n/131072u;
case 18: return n/262144u;
case 19: return n/524288u;
case 20: return n/1048576u;
case 21: return n/2097152u;
case 22: return n/4194304u;
case 23: return n/8388608u;
case 24: return n/16777216u;
case 25: return n/33554432u;
case 26: return n/67108864u;
case 27: return n/134217728u;
case 28: return n/268435456u;
case 29: return n/536870912u;
case 30: return n/1073741824u;
case 31: return n/2147483648u;
}return 0;}
static u32 um(u32 n,int shift){switch(shift){
case 0: return n%1u;
case 1: return n%2u;
case 2: return n%4u;
case 3: return n%8u;
case 4: return n%16u;
case 5: return n%32u;
case 6: return n%64u;
case 7: return n%128u;
case 8: return n%256u;
case 9: return n%512u;
case 10: return n%1024u;
case 11: return n%2048u;
case 12: return n%4096u;
case 13: return n%8192u;
case 14: return n%16384u;
case 15: return n%32768u;
case 16: return n%65536u;
case 17: return n%131072u;
case 18: return n%262144u;
case 19: return n%524288u;
case 20: return n%1048576u;
case 21: return n%2097152u;
case 22: return n%4194304u;
case 23: return n%8388608u;
case 24: return n%16777216u;
case 25: return n%33554432u;
case 26: return n%67108864u;
case 27: return n%134217728u;
case 28: return n%268435456u;
case 29: return n%536870912u;
case 30: return n%1073741824u;
case 31: return n%2147483648u;
}return 0;}
static int runtime_zero(int n){return n/0;}
static int runtime_overflow(int n){return n/-1;}
static u32 values[246]={0u,
1u,
2u,
3u,
4u,
5u,
7u,
8u,
9u,
15u,
16u,
17u,
31u,
32u,
33u,
63u,
64u,
65u,
127u,
128u,
129u,
255u,
256u,
257u,
511u,
512u,
513u,
1023u,
1024u,
1025u,
2047u,
2048u,
2049u,
4095u,
4096u,
4097u,
8191u,
8192u,
8193u,
16383u,
16384u,
16385u,
32767u,
32768u,
32769u,
65535u,
65536u,
65537u,
131071u,
131072u,
131073u,
262143u,
262144u,
262145u,
524287u,
524288u,
524289u,
1048575u,
1048576u,
1048577u,
2097151u,
2097152u,
2097153u,
4194303u,
4194304u,
4194305u,
5894157u,
8388607u,
8388608u,
8388609u,
16777215u,
16777216u,
16777217u,
33554431u,
33554432u,
33554433u,
35561795u,
67108863u,
67108864u,
67108865u,
134217727u,
134217728u,
134217729u,
142157573u,
246998904u,
268435455u,
268435456u,
268435457u,
275417774u,
385897822u,
420023855u,
442712344u,
469928729u,
500796118u,
536870911u,
536870912u,
536870913u,
583871275u,
586343808u,
783375367u,
783682287u,
818058625u,
878097215u,
880814428u,
935699620u,
970543883u,
1056441170u,
1073741823u,
1073741824u,
1073741825u,
1079286565u,
1409699641u,
1465832896u,
1547306145u,
1567696991u,
1580243328u,
1591629347u,
1623312802u,
1638747946u,
1706035516u,
1793985689u,
1824660930u,
1920719946u,
1933528364u,
1936630374u,
2113800587u,
2143478619u,
2147483647u,
2147483648u,
2147483649u,
2280550711u,
2418790376u,
2550326559u,
2580311785u,
2657842339u,
2910621368u,
2917090092u,
3043243367u,
3090515958u,
3162023517u,
3209660242u,
3221225471u,
3221225472u,
3221225473u,
3229478810u,
3349578715u,
3424706007u,
3426929364u,
3514576116u,
3558213678u,
3644980618u,
3758096383u,
3758096384u,
3758096385u,
3848265214u,
3914445335u,
3955827710u,
3967666268u,
3969706715u,
3996814396u,
4008033746u,
4026531839u,
4026531840u,
4026531841u,
4087895485u,
4160749567u,
4160749568u,
4160749569u,
4211755994u,
4227858431u,
4227858432u,
4227858433u,
4261412863u,
4261412864u,
4261412865u,
4278190079u,
4278190080u,
4278190081u,
4286578687u,
4286578688u,
4286578689u,
4290772991u,
4290772992u,
4290772993u,
4292870143u,
4292870144u,
4292870145u,
4293918719u,
4293918720u,
4293918721u,
4294443007u,
4294443008u,
4294443009u,
4294705151u,
4294705152u,
4294705153u,
4294836223u,
4294836224u,
4294836225u,
4294901759u,
4294901760u,
4294901761u,
4294934527u,
4294934528u,
4294934529u,
4294950911u,
4294950912u,
4294950913u,
4294959103u,
4294959104u,
4294959105u,
4294963199u,
4294963200u,
4294963201u,
4294965247u,
4294965248u,
4294965249u,
4294966271u,
4294966272u,
4294966273u,
4294966783u,
4294966784u,
4294966785u,
4294967039u,
4294967040u,
4294967041u,
4294967167u,
4294967168u,
4294967169u,
4294967231u,
4294967232u,
4294967233u,
4294967263u,
4294967264u,
4294967265u,
4294967279u,
4294967280u,
4294967281u,
4294967287u,
4294967288u,
4294967289u,
4294967291u,
4294967292u,
4294967293u,
4294967294u,
4294967295u};
static u32 result[8+246*126+16];
static volatile int codegen_probe[8],side_count;
static int once(void){side_count++;return -513;}
int main(void){
    int win=sc_open("SCCC codegen / mio",300,160);
    if(win<0)return 1;
    char path[64];sc_args(path,sizeof(path));if(!path[0])return 2;
    u8 *bytes=(u8 *)result;char *magic="GOPT1MIO";
    for(int i=0;i<8;i++)bytes[i]=(u8)magic[i];
    result[2]=1;result[3]=246;result[4]=126;
    int out=8,begin=sc_tick();
    for(int i=0;i<246;i++){
        int n=(int)values[i];u32 un=values[i];
        for(int k=0;k<31;k++){result[out++]=(u32)sd(n,k);result[out++]=(u32)sm(n,k);}
        for(int k=0;k<32;k++){result[out++]=ud(un,k);result[out++]=um(un,k);}
    }
    result[5]=(u32)(sc_tick()-begin);
    int a=once()/256,b=once()%256,c=once()*3,d=once()+7,e=once()-7;
    int f=once()&255,g=once()|255,h=once()^255,j=once()<<3,k=once()>>3;
    int z=once()%1,one=once()/1,mixed=(int)((unsigned int)once()/256u);
    // 无符号右操作数沿用原后端选择，另测负值与每次调用只执行一次。
    int errors=a!=-2||b!=-1||c!=-1539||d!=-506||e!=-520||f!=255
        ||g!=-513||h!=-768||j!=-4104||k!=-65||z!=0||one!=-513
        ||mixed!=16777213||side_count!=13;
    result[6]=(u32)errors;result[7]=(u32)side_count;
    for(int i=0;i<16;i++)result[out+i]=0x534D494Fu;
    codegen_probe[1]=sc_write(path,result,sizeof(result));codegen_probe[2]=errors;
    codegen_probe[0]=1;page(win,"Native arithmetic","Esc: return");
    while(sc_key()!=27)sc_yield();return errors;
}
