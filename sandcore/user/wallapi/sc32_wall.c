#include "../sc32/SC32PRIVATE.H"
#include "SC32WALLWIRE.H"
ScStatus ScWallpaperSet(ScU32 mode)
{
    if(mode>SC32_WALL_SAND)return sc32_invalid(SC32_E_RANGE,SC32_W_OPERATION);
    /* 旧入口只在系统库私有层使用；不改变旧SCAPI字节或借忽略的
    * 寄存器增加查询。真实task路径仍核本人配置可变/W权限。 */
    return sc32_legacy(sc32_call(0x18,(ScI32)mode,0,0,0,0,0),SC32_W_OPERATION);
}
ScStatus ScWallpaperQuery(ScWallpaperInfo *out)
{
    return sc32_native(sc32_call(SC32_WIRE_GATE,SC32_W_QUERY,0,(ScI32)out,0,0,0),SC32_W_OPERATION+1);
}
