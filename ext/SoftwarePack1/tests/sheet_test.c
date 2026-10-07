#include "host.h"
#define main sheet_entry
#include "../sheet/sheet.c"
#undef main
HOST_UI()
static void cell(int c,int r,const char *s){sh_set_cell(sh_at(c,r),s);}
int main(void)
{
    sh_grid=calloc(SH_COLS*SH_ROWS,sizeof(sh_cell));cell(0,0,"12.5");cell(1,0,"hello");cell(2,0,"=A1*2");cell(3,0,"=SUM(A1:B1,5)");sh_recalc();CHECK(!strcmp(sh_at(0,0)->disp,"12.5"));CHECK(!strcmp(sh_at(1,0)->disp,"hello"));CHECK(sh_at(2,0)->val==25);CHECK(sh_at(3,0)->val==17.5);
    struct {const char *f;double v;} cases[]={{"=2^3^2",512},{"=-2^2",-4},{"=2^-2",0.25},{"=SQRT(.25)",0.5},{"=AVG(1,2,3)",2},{"=min(5,2,9)",2},{"=COUNT(A1:B1)",1},{"=ABS(-5)",5}};
    for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);i++){cell(4,0,cases[i].f);sh_recalc();CHECK(!(sh_at(4,0)->flags&SH_F_ERROR));CHECK(sh_fabs(sh_at(4,0)->val-cases[i].v)<1e-6);}
    const char *bad[]={"=1/0","=SQRT(-1)","=SUM(A0)","=1junk","=2^1.5","=1e309","=BOGUS(1)"};for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){cell(4,0,bad[i]);sh_recalc();CHECK(sh_at(4,0)->flags&SH_F_ERROR);}
    cell(0,1,"=B2");cell(1,1,"=A2");sh_recalc();CHECK(sh_at(0,1)->flags&SH_F_ERROR);CHECK(sh_at(1,1)->flags&SH_F_ERROR);
    char out[26];sh_fmt(out,26,0.99999);CHECK(!strcmp(out,"1"));sh_fmt(out,26,-0.000001);CHECK(!strcmp(out,"0"));sh_fmt(out,26,12345000000000.0);CHECK(!strcmp(out,"1.2345e13"));
    sh_cell *tmp=calloc(SH_COLS*SH_ROWS,sizeof(sh_cell));CHECK(!sh_csv_parse("\"a,b\",\"a\"\"b\",\"line1\nline2\"\r\n12,=A2*2\r\n",tmp));CHECK(!strcmp(tmp[0].src,"a,b"));CHECK(!strcmp(tmp[1].src,"a\"b"));CHECK(!strcmp(tmp[2].src,"line1\nline2"));CHECK(sh_csv_parse("\"broken",tmp)<0);CHECK(sh_csv_parse("\"a\"z",tmp)<0);free(tmp);
    cell(0,1,"42");cell(1,1,"=A2*2");sh_recalc();sh_save("HOME/TEST.CSV");CHECK(host_get("HOME/TEST.CSV")!=0);cell(0,1,"99");sh_load("HOME/TEST.CSV");CHECK(sh_at(1,1)->val==84);
    host_put("HOME/BAD.CSV","\"unterminated",13);sh_load("HOME/BAD.CSV");CHECK(sh_at(1,1)->val==84);host_fail_read=1;sh_load("HOME/TEST.CSV");CHECK(sh_at(1,1)->val==84);host_fail_read=0;
    host_fail_alloc=1;sh_load("HOME/TEST.CSV");CHECK(sh_at(1,1)->val==84);host_fail_write=1;sh_dirty=1;sh_save("HOME/TEST.CSV");CHECK(sh_dirty);host_fail_write=0;
    sh_insert_row(0);CHECK(!strcmp(sh_at(1,2)->src,"=A3*2"));CHECK(sh_at(1,2)->val==84);sh_adjust_refs(2,0);CHECK(!strcmp(sh_at(1,2)->src,"=#REF"));
    cell(0,0,"Product");cell(1,0,"Amount");cell(0,1,"Armor kit");cell(1,1,"12.5");cell(2,1,"=B2*8");cell(0,2,"line1\nline2");sh_recalc();CHECK(!strchr(sh_at(0,2)->disp,'\n'));
    host_ui(960,640,100,0);sh_draw();host_image("sheet-aurora");host_ui(641,481,200,0);sh_draw();CHECK(sh_visible_cols>0&&sh_visible_rows>0);host_image("sheet-200");sh_sel_col=25;sh_sel_row=127;sh_draw();CHECK(sh_scroll_x+sh_visible_cols>25);CHECK(sh_scroll_y+sh_visible_rows>127);
    sh_editing=1;copy(sh_edit,"unsaved formula",SH_SRC);host_key_input=27;sh_action(PK_ABOUT_ID);CHECK(sh_editing&&!strcmp(sh_edit,"unsaved formula"));host_image("about-sheet-200");host_report("sheet");return 0;
}
