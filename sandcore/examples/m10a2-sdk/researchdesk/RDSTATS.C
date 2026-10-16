#include "RDSTATS.H"
static void zero(void *p,ScU32 n){
    for(ScU32 i=0;i<n;i++)((unsigned char *)p)[i]=0;
}
static int valid(const RdStatsState *s){
    return s && s->size==sizeof(*s) && s->version==1;
}
ScStatus RdStatsBegin(RdStatsState *s)
{
    if(!s || s->size!=sizeof(*s) || s->version!=1)return -1;
    zero(s,sizeof(*s));
    s->size=sizeof(*s);
    s->version=1;
    return 0;
}
ScStatus RdStatsPush(RdStatsState *s,const ScI32 *values,ScU32 count)
{
    if(!valid(s) || (count && !values) || count>0xffffffffu-s->count)return -1;
    for(ScU32 i=0;i<count;i++){
        ScI32 value=values[i];
        ScU32 old=s->sum_lo;
        s->sum_lo+=(ScU32)value;
        s->sum_hi+=(value<0?0xffffffffu:0)+(s->sum_lo<old);
        if(!s->count || value<s->minimum)s->minimum=value;
        if(!s->count || value>s->maximum)s->maximum=value;
        if(value<0)s->negative++;
        else if(value>0)s->positive++;
        else s->zero++;
        s->count++;
    }
    return 0;
}
static ScU32 divide(ScU32 hi,ScU32 lo,ScU32 denominator,ScU32 *qhi,ScU32 *qlo)
{
    ScU32 remainder=0;
    *qhi=*qlo=0;
    /* 逐位除法的余数还原携带第33位，不能让r<<1溢出后漏掉减法。 */
    for(int bit=63;bit>=0;bit--){
        ScU32 carry=remainder>>31;
        remainder=(remainder<<1)|(bit>=32?(hi>>(bit-32))&1u:(lo>>bit)&1u);
        if(carry || remainder>=denominator){
            remainder-=denominator;
            if(bit>=32)*qhi|=1u<<(bit-32);
            else *qlo|=1u<<bit;
        }
    }
    return remainder;
}
ScStatus RdStatsFinish(const RdStatsState *s,RdStatsResult *out)
{
    if(!valid(s) || !out || out->size<sizeof(*out) || out->version!=1)return -1;
    RdStatsResult result;
    zero(&result,sizeof(result));
    result.size=sizeof(result);
    result.version=1;
    result.count=s->count;
    result.positive=s->positive;
    result.negative=s->negative;
    result.zero=s->zero;
    result.minimum=s->minimum;
    result.maximum=s->maximum;
    ScU32 hi=s->sum_hi,lo=s->sum_lo;
    int negative=(hi&0x80000000u)!=0;
    if(negative){
        lo=0u-lo;
        hi=~hi+(lo==0);
    }
    if(s->count){
        ScU32 qhi,qlo;
        divide(hi,lo,s->count,&qhi,&qlo);
        if(qhi || qlo>(negative?0x80000000u:0x7fffffffu))return -2;
        result.mean=negative?(ScI32)(0u-qlo):(ScI32)qlo;
    }
    char reverse[21];
    ScU32 used=0;
    do {
        ScU32 qhi,qlo;
        ScU32 digit=divide(hi,lo,10,&qhi,&qlo);
        reverse[used++]=(char)('0'+digit);
        hi=qhi;
        lo=qlo;
    }
    while(hi || lo);
    ScU32 at=0;
    if(negative)result.sum[at++]='-';
    while(used)result.sum[at++]=reverse[--used];
    result.sum[at]=0;
    *out=result;
    return 0;
}
