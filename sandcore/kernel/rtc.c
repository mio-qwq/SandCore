#include "rtc.h"
static u8 read(u8 index){outb(0x70,index);return inb(0x71);}
static int bcd(u8 value){return (value&15)+10*(value>>4);}
static int leap(int year){return !(year%4) && (year%100 || !(year%400));}
int rtc_snapshot(u32 out[8])
{
    /* UIP附近只作有界重读，不能在系统调用中等一整秒挡住调度。
     * 先后两份完整字段及模式相等才发布；QEMU测试显式RTC=UTC。
     * 暂以2000..2099解释两位年份，不猜设备未知的century寄存器。 */
    u8 first[7],last[7];static const u8 registers[7]={0,2,4,7,8,9,11};int stable=0;
    for(int tries=0;tries<8;tries++){if(read(10)&0x80)continue;for(int i=0;i<7;i++)first[i]=read(registers[i]);
        if(read(10)&0x80)continue;for(int i=0;i<7;i++)last[i]=read(registers[i]);if(read(10)&0x80)continue;
        stable=1;for(int i=0;i<7;i++)if(first[i]!=last[i])stable=0;if(stable)break;}
    if(!stable)return -6;int binary=first[6]&4,hour=first[2]&0x7F,pm=first[2]&0x80;
    int second=binary?first[0]:bcd(first[0]),minute=binary?first[1]:bcd(first[1]),day=binary?first[3]:bcd(first[3]);
    int month=binary?first[4]:bcd(first[4]),year=2000+(binary?first[5]:bcd(first[5]));if(!binary)hour=bcd((u8)hour);
    if(!(first[6]&2)){if(hour<1 || hour>12)return -2;hour=hour%12+(pm?12:0);}static const int days[12]={31,28,31,30,31,30,31,31,30,31,30,31};
    if(second>59 || minute>59 || hour>23 || month<1 || month>12 || day<1 || day>days[month-1]+(month==2 && leap(year)) || year>2099)return -2;
    out[0]=1;out[1]=(u32)year;out[2]=(u32)month;out[3]=(u32)day;out[4]=(u32)hour;out[5]=(u32)minute;out[6]=(u32)second;out[7]=0;return 0;
}
