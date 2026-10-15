#include "../sc32/SC32PRIVATE.H"
#include "SC32FONTWIRE.H"

typedef char sc32_font_info_96[(sizeof(ScFontInfo)==96)?1:-1];
typedef char sc32_font_desc_32[(sizeof(ScFontGlyphDesc)==32)?1:-1];
typedef char sc32_font_glyph_1600[(sizeof(ScFontGlyph)==1600)?1:-1];

ScStatus ScFontQuery(ScU32 face,ScFontInfo *out)
{
    Sc32FontQueryRequest request={16,1,face,0};
    return sc32_native(sc32_call(SC32_WIRE_GATE,SC32_FONT_QUERY,(ScI32)&request,
                               (ScI32)out,0,0,0),SC32_FONT_OPERATION_QUERY);
}
ScStatus ScFontGetGlyph(const ScFontGlyphDesc *description,ScFontGlyph *out)
{
    return sc32_native(sc32_call(SC32_WIRE_GATE,SC32_FONT_GLYPH,(ScI32)description,
                               (ScI32)out,0,0,0),SC32_FONT_OPERATION_GLYPH);
}
