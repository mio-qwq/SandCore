#include "host.h"
#define main raider_entry
#include "../raider/raider.c"
#undef main
HOST_UI()
static void empty_arena(void)
{for(int y=0;y<24;y++)for(int x=0;x<24;x++)rd_grid[y][x]=x==0||y==0||x==23||y==23;rd_px=5.5f;rd_py=5.5f;rd_ang=0;rd_en_count=rd_it_count=0;rd_state=1;rd_cd=rd_reload=0;}
static void map_paths(void)
{
    for(int level=0;level<6;level++){
        rd_load_level(level);CHECK(rd_en_count>0&&rd_en_count<40);CHECK(rd_it_count>0&&rd_it_count<48);
        int sx=(int)rd_px,sy=(int)rd_py,keys=0,found_exit=0,seen[24][24],queue[576];
        for(int pass=0;pass<3;pass++){
            memset(seen,0,sizeof(seen));int head=0,tail=0;queue[tail++]=sy*24+sx;seen[sy][sx]=1;
            while(head<tail){int id=queue[head++],x=id%24,y=id/24;
                for(int i=0;i<rd_it_count;i++)if((int)rd_it[i].x==x&&(int)rd_it[i].y==y){if(rd_it[i].type==8)keys|=1;if(rd_it[i].type==9)keys|=2;}
                int dx[]={-1,1,0,0},dy[]={0,0,-1,1};for(int k=0;k<4;k++){int nx=x+dx[k],ny=y+dy[k];if(nx<0||nx>=24||ny<0||ny>=24)continue;int t=rd_grid[ny][nx];if(t==7){found_exit=1;continue;}if(seen[ny][nx]||t==1||t==2||t==3||(t==4&&!(keys&1))||(t==5&&!(keys&2)))continue;seen[ny][nx]=1;queue[tail++]=ny*24+nx;}
            }
        }
        CHECK(keys==3&&found_exit);for(int i=0;i<rd_it_count;i++)CHECK(seen[(int)rd_it[i].y][(int)rd_it[i].x]);
    }
}
int main(void)
{
    rd_trig_init();rd_gen_textures();rd_gen_sprites();rd_sound=0;ui_focus=1;CHECK(rd_floorf(-1)==-1);CHECK(rd_floorf(-1.2f)==-2);CHECK(rd_fabsf2(rd_sqrtf2(.25f)-.5f)<.0001f);map_paths();
    rd_start_game();CHECK(rd_state==8&&rd_clip[0]==12&&rd_hp==100);rd_score+=500;rd_hp=1;rd_retry();CHECK(rd_score==0&&rd_hp==100);
    empty_arena();rd_reset_player();rd_en_count=1;rd_en[0]=(rd_enemy){7.5f,5.5f,0,28,1,0,0,1,0};rd_fire();CHECK(rd_clip[0]==11&&rd_en[0].hp==6);rd_cd=0;rd_fire();CHECK(!rd_en[0].alive&&rd_kills>0);
    rd_clip[0]=0;rd_bullets=3;rd_cd=0;rd_reload_begin();CHECK(rd_reload==80);rd_en_count=0;for(int i=0;i<80;i++)rd_sim();CHECK(rd_clip[0]==3&&rd_bullets==0&&!rd_reload);
    empty_arena();rd_en_count=1;rd_en[0]=(rd_enemy){7.5f,5.5f,0,28,1,0,0,0,0};rd_grid[5][6]=1;rd_hitscan(0,100);CHECK(rd_en[0].hp==28);float px=rd_px;rd_move(&rd_px,&rd_py,1,0,.25);CHECK(rd_px==px);
    empty_arena();rd_en_count=1;rd_en[0]=(rd_enemy){6.75f,5.5f,0,28,1,0,0,1,20};rd_hp=100;rd_armor=0;rd_enemies_tick();CHECK(rd_hp<100);
    empty_arena();rd_mouse_captured=0;host_keys['w']=1;rd_sim();float distance=rd_px-5.5f;rd_px=5.5f;host_keys[0x80]=1;rd_sim();CHECK(rd_fabsf2((rd_px-5.5f)-distance)<.00001f);memset(host_keys,0,sizeof(host_keys));
    rd_sensitivity=3;rd_sensitivity_update();CHECK(rd_sens<.001f);rd_adjust_sens(-100);CHECK(rd_sensitivity==1);rd_adjust_sens(100);CHECK(rd_sensitivity==20);
    empty_arena();rd_grid[5][6]=7;rd_px=5.5;rd_key_red=rd_key_blue=0;rd_use();CHECK(rd_state==1);rd_key_red=rd_key_blue=1;rd_level=5;rd_boss_dead=0;rd_use();CHECK(rd_state==1);rd_boss_dead=1;rd_use();CHECK(rd_state==2);
    rd_start_game();host_ui(960,640,100,0);rd_geometry();CHECK(!rd_ensure_buffer());rd_state=1;rd_render_world();rd_blit();rd_draw_weapon();rd_draw_target();rd_draw_map();rd_draw_hud();host_image("raider-aurora");
    host_ui(641,481,200,0);rd_geometry();rd_render_shift=1;rd_geometry();CHECK(!rd_ensure_buffer());rd_render_world();rd_blit();rd_draw_weapon();rd_draw_hud();host_image("raider-200");
    rd_draw_menu();host_image("raider-menu-200");rd_draw_settings();host_image("raider-settings-200");rd_draw_guide();host_image("raider-guide-200");host_ui(960,640,100,0);rd_draw_guide();host_image("raider-guide");
    rd_state=1;rd_mouse_captured=1;int elapsed=rd_elapsed,hp=rd_hp;host_key_input=27;rd_action(PK_ABOUT_ID);CHECK(rd_state==5&&!rd_mouse_captured&&rd_hp==hp&&rd_elapsed==elapsed);host_image("about-raider");host_report("raider");return 0;
}
