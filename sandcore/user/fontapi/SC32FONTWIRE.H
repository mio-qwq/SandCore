#ifndef SANDCORE_SC32FONTWIRE_H
#define SANDCORE_SC32FONTWIRE_H
#include "../SC32FONT.H"

/* 私有gate记录；正式应用只包含SC32FONT.H并调用cdecl导出。 */
#define SC32_FONT_QUERY 0x600u
#define SC32_FONT_GLYPH 0x601u
#define SC32_FONT_OPERATION_QUERY 0xa00u
#define SC32_FONT_OPERATION_GLYPH 0xa01u
typedef struct { ScU32 size,version,face,reserved; } Sc32FontQueryRequest;
#endif
