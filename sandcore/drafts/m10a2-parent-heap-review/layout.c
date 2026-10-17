/* 只把实际旧运行源的私有布局转成i386整数表，不链接或运行这些函数。 */
#include "user/scui/SCUIPRIVATE.H"
#include "user/sclib/sclib_hash.c"
#include "user/sclib/sclib_cache.c"
#include "user/stdlib/memory.c"
#define OFF(type,field) __builtin_offsetof(type,field)
const unsigned int heap_layout[] __attribute__((section(".heaplayout"),used))={
    0x48454150,1,sizeof(__scc_arena),sizeof(__scc_block),
    OFF(ScUiContext,glyphs),OFF(ScResourceCache,first),OFF(ScResourceCache,count),
    OFF(ScResourceCache,budget),OFF(ScResourceCache,used),
    sizeof(SlCacheEntry),OFF(SlCacheEntry,next),OFF(SlCacheEntry,key),
    OFF(SlCacheEntry,key_bytes),OFF(SlCacheEntry,resource),OFF(SlCacheEntry,valid),
    sizeof(UiGlyphKey),sizeof(ScFontGlyph)
};
