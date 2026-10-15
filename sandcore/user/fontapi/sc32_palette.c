#include "../sc32/SC32PRIVATE.H"
#include "../SC32PALETTE.H"

ScStatus ScDisplayPalette(ScU32 *rgb,ScU32 capacity)
{
    if(capacity<256u || capacity>16384u)return sc32_invalid(SC32_E_RANGE,0x509u);
    ScStatus checked=sc32_out(rgb,capacity*4u);
    if(checked<0)return sc32_invalid(checked,0x509u);
    /* 旧PALETTEINFO为0x27，固定1024B。整声明输出已先验，仍只让
     * 旧服务写原256项；SCB1/SCF得到真实槽色，不另复制一份裸色表。 */
    return sc32_legacy(sc32_call(0x27,(ScI32)rgb,0,0,0,0,0),0x509u);
}
