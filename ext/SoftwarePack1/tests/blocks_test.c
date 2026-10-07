#include "host.h"
#define main blocks_entry
#include "../blocks/blocks.c"
#undef main
HOST_UI()
int main(void)
{
    ex_rand_seed(123);bk_bag_count=0;for(int r=0;r<100;r++){int mask=0;for(int i=0;i<7;i++)mask|=1<<bk_bag_next();CHECK(mask==127);}
    bk_sound=0;bk_new_game();bk_hold_swap();int piece=bk_piece,hold=bk_hold;CHECK(bk_hold_used);bk_hold_swap();CHECK(bk_piece==piece&&bk_hold==hold);bk_drop(1);CHECK(!bk_hold_used);bk_hold_swap();CHECK(bk_hold_used);
    bk_new_game();int bottom;bk_ghost_y(&bottom);bk_py=bottom;int original=bk_piece;for(int i=0;i<34;i++)bk_gravity();CHECK(bk_lock_wait==34&&bk_piece==original);bk_gravity();CHECK(bk_lock_wait==0);int occupied=0;for(int y=0;y<20;y++)for(int x=0;x<10;x++)if(bk_grid[y][x])occupied++;CHECK(occupied==4);
    bk_new_game();for(int y=16;y<20;y++)for(int x=0;x<10;x++)bk_grid[y][x]=1;bk_clear_lines();CHECK(bk_lines==4);CHECK(bk_score>=800);CHECK(bk_b2b);
    bk_new_game();bk_py=-2;bk_lock();CHECK(bk_state==2);
    bk_new_game();bk_piece=0;bk_hold=0;bk_ghost=1;host_ui(960,640,100,0);bk_draw();CHECK(bk_fx>0&&bk_fy+20*bk_cell<UI_H-22);host_image("blocks-aurora");
    host_ui(641,481,200,0);bk_draw();CHECK(bk_fx-bk_side-12>=0);CHECK(bk_fx+10*bk_cell+12+bk_side<=UI_W);host_image("blocks-200");host_ui(960,640,100,1);bk_draw();host_image("blocks-night");
    int y=bk_py,score=bk_score;host_key_input=27;bk_show_about();CHECK(bk_state==1&&bk_py==y&&bk_score==score);host_image("about-blocks-night");host_report("blocks");return 0;
}
