/* 各GUI只缓存本人位图副本。完整TTF的基线/字宽通过新接口取得，
 * 旧内核不认识新入口时仍走GLYPH16；历史SCX字节不为此重编。
 * 缓存满后轮转替换，不能把第257个字符永远挤在最后一槽造成抖动。
 * 隐藏时调用者不绘画；字体版本改变才清字形，尺寸缩放不换字源。 */
static u32 ui_glyph_codes[256];
static u16 ui_glyph_rows[256][16];
static int ui_glyph_width[256],ui_glyph_count,ui_glyph_cursor;
static u32 ui_font_generation;
static u32 ui_font_bitmap[SC_FONT_BITMAP_WORDS];
static inline int ui_font_refresh(void)
{
    u32 info[16];
    if(sc_fontinfo2(16,info) || info[0]!=1 || info[1]==ui_font_generation)return 0;
    ui_font_generation=info[1];ui_glyph_count=ui_glyph_cursor=0;return 1;
}
static inline int ui_glyph_slot(u32 scalar)
{
    int slot=0;while(slot<ui_glyph_count && ui_glyph_codes[slot]!=scalar)slot++;
    if(slot<ui_glyph_count)return slot;
    if(slot==256){slot=ui_glyph_cursor++;if(ui_glyph_cursor==256)ui_glyph_cursor=0;}
    else ui_glyph_count++;
    ui_glyph_codes[slot]=scalar;
    int width=sc_glyph_bitmap(scalar,16,16,ui_font_bitmap,SC_FONT_BITMAP_WORDS);
    if(width>=0 && ui_font_bitmap[0]==1){
        int left=(int)ui_font_bitmap[9],w=(int)ui_font_bitmap[6],h=(int)ui_font_bitmap[7];
        for(int row=0;row<16;row++){
            int bits=0;
            if(row<h)for(int col=0;col<16;col++){
                int x=col-left;
                if(x>=0 && x<w && (ui_font_bitmap[16+row*4+x/32]&(1u<<(31-(x&31)))))bits|=0x8000u>>col;
            }
            ui_glyph_rows[slot][row]=(u16)bits;
        }
    }else width=sc_glyph16(scalar,ui_glyph_rows[slot]);
    ui_glyph_width[slot]=width>0?(width<=8?8:16):16;return slot;
}
static inline int ui_scalar_width(u32 scalar)
{return ui_glyph_width[ui_glyph_slot(scalar)];}
