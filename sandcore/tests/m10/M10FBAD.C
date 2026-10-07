#include "SCIO.H"
/* 预期脸掩码来自独立坏字体副本的清单；只查询真实公开状态。
 * 被拒绝的脸不得留半张字库、暂存页面或改写失败调用的缓冲。 */
static u32 info[18],bitmap[SC_FONT_BITMAP_WORDS+2];
static int failures;
static void check(int okay,const char *message)
{cli_text(1,okay?"PASS ":"FAIL ");cli_text(1,message);cli_text(1,"\n");if(!okay)failures++;}
int main(void)
{
    int mask;if(cli_parse()<0 || cli_argc!=1 || cli_integer(cli_argv[0],&mask)<0 || mask<0 || mask>3)return 2;
    for(int i=0;i<2;i++){
        u32 face=i?16u:12u;int active=(mask&(1<<i))!=0;
        info[0]=0xA10B0001;info[17]=0xA10B0002;
        int result=sc_fontinfo2(face,info+1);
        check(result==0 && info[0]==0xA10B0001 && info[17]==0xA10B0002,"font info preserves exact legacy-independent boundary");
        check(info[1]==1 && info[3]==face && info[4]==(u32)active,"only complete expected TTF face is active");
        if(active)check(info[2]>0 && info[5]>0 && info[6]==7540 && info[11]>0 && info[12]>0,"unrelated original face remains complete with owned pages");
        else check(!info[2] && !info[5] && !info[6] && !info[11] && !info[12],"rejected TTF has zero glyphs epoch byte and temporary pages");
        for(u32 j=0;j<SC_FONT_BITMAP_WORDS+2;j++)bitmap[j]=0xA10B0003;
        result=sc_glyph_bitmap(0x4E2D,face,face,bitmap+1,SC_FONT_BITMAP_WORDS);
        if(active)check(result>0 && bitmap[1]==1 && bitmap[4]==1
            && bitmap[0]==0xA10B0003 && bitmap[SC_FONT_BITMAP_WORDS+1]==0xA10B0003,"complete other face still returns real Chinese bitmap");
        else{
            int unchanged=1;for(u32 j=0;j<SC_FONT_BITMAP_WORDS+2;j++)if(bitmap[j]!=0xA10B0003)unchanged=0;
            check(result==-1 && unchanged,"inactive face bitmap fails without modifying any output word");
        }
    }
    u16 rows[18];rows[0]=0xA10B;rows[17]=0xA10C;
    check(sc_glyph16(0x41,rows+1)==8 && rows[0]==0xA10B && rows[17]==0xA10C,"old ASCII glyph16 works and preserves 32 byte bounds");
    int ink=0;for(int i=1;i<17;i++)if(rows[i])ink=1;check(ink,"old ASCII fallback has actual visible glyph pixels");
    /* 旧FONT8公开ABI是128字×8行，1024B；95字仅是旧资源中
     * 可打印字形数，绝不能把它误当输出大小而让夹具自己越界。 */
    u8 font[1026];font[0]=0xA1;font[1025]=0xA2;
    check(sc_font8(font+1)==1024 && font[0]==0xA1 && font[1025]==0xA2,"old 128 glyph font8 preserves original 1024 byte layout");
    cli_text(1,"bad_font_failures=");cli_number(1,failures);cli_text(1,"\n");return failures?1:0;
}
