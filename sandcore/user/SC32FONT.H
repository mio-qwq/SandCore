#ifndef SANDCORE_SC32FONT_H
#define SANDCORE_SC32FONT_H
#include "SC32API.H"

/* 系统字库只给值和位图副本；字库、解析暂存及全局缓存仍归内核。
 * 公开字形布局与旧400字syscall布局分别版本化，不依赖旧SCAPI头。 */
enum {
    SC32_CAP_FONT_GLYPHS=262144,SC32_FONT_PHOENIX12=12,SC32_FONT_PHOENIX16=16,
    SC32_FONT_SOURCE_NONE=0,SC32_FONT_SOURCE_PHOENIX_TTF=1,SC32_FONT_MONO=1,
    SC32_FONT_ROWS=96,SC32_FONT_ROW_WORDS=4
};

typedef struct {
    ScU32 size,version,face,generation,active,source_kind,glyph_count,mapped_count;
    ScU32 units_per_em;
    ScI32 ascent,descent,line_gap;
    ScU32 data_bytes,face_pages,cache_pages,cache_hits,cache_misses;
    ScU32 horizontal_metrics,native_pixels,flags,reserved[4];
} ScFontInfo;
typedef struct {
    ScU32 size,version,face,scalar,pixels,expected_generation,flags,reserved;
} ScFontGlyphDesc;
typedef struct {
    ScU32 size,version,face,generation,glyph_id,mapped,pixels;
    ScI32 advance;
    ScU32 width,height;
    ScI32 baseline,left,bearing_26_6,advance_26_6;
    ScU32 flags,reserved;
    ScU32 rows[SC32_FONT_ROWS][SC32_FONT_ROW_WORDS];
} ScFontGlyph;

/* Query允许未激活脸，active=0是实际状态；Get返回NOT_FOUND。
 * expected_generation=0取当前脸，非零不匹配为STALE，错误不改out。
 * 合法缺字仍OK：mapped=0，返回同脸.notdef及它的真实前进宽。
 * pixels为1..64，位图bit31在左，未用行列为0；请求原生尺寸重新
 * 栅格化，不能把位图整体放大替代缩放后的字形。 */
ScStatus ScFontQuery(ScU32 face,ScFontInfo *out);
ScStatus ScFontGetGlyph(const ScFontGlyphDesc *description,ScFontGlyph *out);
#endif
