#ifndef SANDCORE_SC32PALETTE_H
#define SANDCORE_SC32PALETTE_H
#include "SCTYPES.H"

/* capacity以颜色项计256..16384；完整本人输出RW先验。成功只写
 * 256项0x00RRGGBB原DAC对应值，声明的未知尾部保持，错误不改out。 */
ScStatus ScDisplayPalette(ScU32 *rgb,ScU32 capacity);
#endif
