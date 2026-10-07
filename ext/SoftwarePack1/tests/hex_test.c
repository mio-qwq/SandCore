#include "host.h"
#define main hex_entry
#include "../hexed/hexed.c"
#undef main
HOST_UI()
int main(void)
{
    CHECK(!hx_alloc_buffers(256));hx_size=256;for(int i=0;i<256;i++)hx_data[i]=i;
    hx_type_hex(15);hx_type_hex(1);CHECK(hx_data[0]==0xF1&&hx_off==1);hx_nibble=1;hx_undo();CHECK(hx_data[0]==0&&!hx_nibble);hx_redo();CHECK(hx_data[0]==0xF1);
    host_put("HOME/ONE.BIN","abc",3);hx_load("HOME/ONE.BIN");CHECK(hx_size==3&&!memcmp(hx_data,"abc",3));host_fail_read=1;hx_load("HOME/ONE.BIN");CHECK(hx_size==3&&!memcmp(hx_data,"abc",3));host_fail_read=0;host_fail_alloc=1;hx_load("HOME/ONE.BIN");CHECK(hx_size==3&&!memcmp(hx_data,"abc",3));
    hx_parse_pattern("0xABC");CHECK(!hx_pat_len);hx_parse_pattern("0x12XX");CHECK(!hx_pat_len);hx_parse_pattern("AB CD");CHECK(hx_pat_len==2&&hx_pat_bytes[1]==0xCD);hx_parse_pattern("abc");CHECK(hx_pat_len==3&&!hx_pat_hex);
    hx_parse_pattern("z");hx_off=1;hx_find_next();CHECK(hx_off==1);hx_parse_pattern("a");hx_find_next();CHECK(hx_off==0);
    host_put("HOME/EMPTY.BIN","",0);hx_load("HOME/EMPTY.BIN");CHECK(hx_size==0&&hx_off==0);hx_find_next();CHECK(hx_off==0);
    host_put("HOME/DATA.BIN","SandCore native byte editor",27);hx_load("HOME/DATA.BIN");host_ui(960,640,100,0);hx_draw();host_image("hex-aurora");host_ui(641,481,200,0);hx_draw();CHECK(hx_per_row==4);host_image("hex-200");
    hx_dirty=hx_nibble=1;host_key_input=27;hx_action(PK_ABOUT_ID);CHECK(hx_dirty&&hx_nibble&&hx_size==27&&!memcmp(hx_data,"SandCore",8));host_image("about-hex-200");hx_free_all();host_report("hex");return 0;
}
