/* 仿真只在游玩且聚焦时推进，输入快照每帧消费一次。PS/2 原始
 * 相对计数与显示缩放无关；绝不能把一个鼠标位移重复用于补步。 */
static void rd_sensitivity_update(void)
{rd_sensitivity=ex_clamp(rd_sensitivity,1,20);rd_sens=0.00018f+rd_sensitivity*0.00012f;}
static void rd_release_mouse(void)
{sc_mouse_capture(ui_win,0);rd_mouse_captured=rd_mouse_fire=0;}
static int rd_try_capture(void)
{rd_mouse_captured=sc_mouse_capture(ui_win,1)==0;return rd_mouse_captured;}
static void rd_pause(void)
{rd_release_mouse();rd_state=5;ui_followup=1;}
static void rd_reload_begin(void)
{
    if(rd_weapon==3||rd_reload||rd_clip[rd_weapon]>=rd_capacity[rd_weapon])return;
    int reserve=rd_weapon==1?rd_shells:rd_bullets;
    if(!reserve){rd_msg_set("No reserve ammo. Switch weapon or use blade.");return;}
    rd_reload=rd_weapon==1?120:rd_weapon==2?160:80;rd_msg_set("Reloading...");
    rd_sfx(360,70,EXWAVE_TRIANGLE,100);
}
static void rd_checkpoint(void)
{
    rd_checkpoint_score=rd_score;rd_checkpoint_hp=rd_hp;rd_checkpoint_armor=rd_armor;
    rd_checkpoint_bullets=rd_bullets;rd_checkpoint_shells=rd_shells;rd_checkpoint_weapon=rd_weapon;
    rd_checkpoint_shotgun=rd_has_shotgun;rd_checkpoint_chain=rd_has_chain;
    for(int i=0;i<3;i++)rd_checkpoint_clips[i]=rd_clip[i];
}
static void rd_reset_player(void)
{
    rd_hp=100;rd_armor=25;rd_bullets=72;rd_shells=8;rd_weapon=0;
    rd_has_shotgun=rd_has_chain=rd_key_red=rd_key_blue=0;
    rd_clip[0]=12;rd_clip[1]=rd_clip[2]=0;
    rd_flash=rd_fire_vis=rd_cd=rd_reload=rd_hitmark=rd_walk=0;
    rd_msg[0]=0;rd_msg_ticks=0;
}
static void rd_start_game(void)
{rd_score=0;rd_reset_player();rd_load_level(0);rd_checkpoint();rd_state=8;ui_followup=1;}
static void rd_retry(void)
{
    rd_score=rd_checkpoint_score;rd_hp=rd_checkpoint_hp;rd_armor=rd_checkpoint_armor;
    rd_bullets=rd_checkpoint_bullets;rd_shells=rd_checkpoint_shells;rd_weapon=rd_checkpoint_weapon;
    rd_has_shotgun=rd_checkpoint_shotgun;rd_has_chain=rd_checkpoint_chain;
    for(int i=0;i<3;i++)rd_clip[i]=rd_checkpoint_clips[i];
    rd_flash=rd_fire_vis=rd_cd=rd_reload=rd_hitmark=rd_walk=0;
    rd_load_level(rd_level);rd_state=8;ui_followup=1;
}
static void rd_best_save(void)
{
    if(rd_score<=rd_best)return;
    char out[16];ex_dec(out,16,rd_score);
    if(sc_write(RD_SCORE_FILE,out,length(out))==length(out))rd_best=rd_score;
    else rd_msg_set("Best score could not be saved.");
}
static void rd_next_level(void)
{
    if(rd_level+1>=RD_LEVELS){rd_state=4;rd_best_save();return;}
    rd_hp=ex_min(100,rd_hp+25);rd_bullets=ex_min(240,rd_bullets+24);rd_shells=ex_min(60,rd_shells+6);
    rd_flash=rd_fire_vis=rd_cd=rd_reload=rd_hitmark=0;
    rd_load_level(rd_level+1);rd_checkpoint();rd_state=8;ui_followup=1;
}
static int rd_hud_height(void){return UI_W<560?110:76;}
static void rd_geometry(void)
{
    rd_view_w=ui_width;rd_view_h=ex_max(1,ui_height-ui_px(rd_hud_height()));
    rd_scale=1<<rd_render_shift;
    rd_rw=ex_clamp((rd_view_w+rd_scale-1)/rd_scale,1,1920);
    rd_rh=ex_max(1,(rd_view_h+rd_scale-1)/rd_scale);
}
static int rd_ensure_buffer(void)
{
    static int old_w,old_h;
    if(rd_buf&&old_w==rd_rw&&old_h==rd_rh)return 0;
    u32 *next=sc_alloc((u32)rd_rw*rd_rh*4);if(!next)return -1;
    if(rd_buf)sc_free(rd_buf);rd_buf=next;old_w=rd_rw;old_h=rd_rh;return 0;
}
static void rd_discover(void)
{
    int px=(int)rd_px,py=(int)rd_py;
    for(int y=ex_max(0,py-3);y<=ex_min(RD_MH-1,py+3);y++)
        for(int x=ex_max(0,px-3);x<=ex_min(RD_MW-1,px+3);x++)
            if(rd_los(rd_px,rd_py,x+0.5f,y+0.5f)||ex_abs(x-px)+ex_abs(y-py)<2)rd_seen[y][x]=1;
}
static void rd_sim(void)
{
    if(rd_state!=1||!ui_focus)return;
    rd_elapsed++;if(rd_flash)rd_flash--;if(rd_fire_vis)rd_fire_vis--;
    if(rd_msg_ticks)rd_msg_ticks--;if(rd_cd)rd_cd--;if(rd_hitmark)rd_hitmark--;
    if(rd_reload&&!--rd_reload){
        int need=rd_capacity[rd_weapon]-rd_clip[rd_weapon];int *reserve=rd_weapon==1?&rd_shells:&rd_bullets;
        if(need>*reserve)need=*reserve;rd_clip[rd_weapon]+=need;*reserve-=need;rd_msg_set("Reloaded");
    }
    int fwd=sc_down('w')-sc_down('s'),str=sc_down('d')-sc_down('a');
    if(!rd_mouse_captured){fwd+=sc_down(0x80)-sc_down(0x81);}
    fwd=ex_clamp(fwd,-1,1);str=ex_clamp(str,-1,1);
    rd_ang+=(sc_down(0x83)-sc_down(0x82))*0.018f;
    while(rd_ang<0)rd_ang+=6.2831853f;while(rd_ang>=6.2831853f)rd_ang-=6.2831853f;
    if(fwd||str){
        float speed=(fwd&&str)?0.0226f:0.032f,fx=rd_cos(rd_ang),fy=rd_sin(rd_ang);
        rd_move(&rd_px,&rd_py,fx*speed*fwd-fy*speed*str,fy*speed*fwd+fx*speed*str,0.25f);rd_walk++;
    }
    rd_pickups();rd_enemies_tick();
    if(rd_state==1&&(rd_mouse_fire||sc_down(' ')))rd_fire();
    if(rd_elapsed%12==0)rd_discover();
}
static void rd_load_config(void)
{
    char cfg[2048],v[24];
    if(pk_read_text("HOME/RAIDER.CFG",cfg,sizeof(cfg))>=0){
        int n;
        if(!ex_cfg_get(cfg,"difficulty",v,24)&&!ex_parse_int(v,&n))rd_difficulty=ex_clamp(n,1,3);
        if(!ex_cfg_get(cfg,"sensitivity",v,24)&&!ex_parse_int(v,&n))rd_sensitivity=ex_clamp(n,1,20);
        if(!ex_cfg_get(cfg,"render",v,24)&&!ex_parse_int(v,&n))rd_render_shift=ex_clamp(n,1,2)-1;
        if(!ex_cfg_get(cfg,"sound",v,24))rd_sound=!equal(v,"0");
        if(!ex_cfg_get(cfg,"minimap",v,24))rd_minimap=!equal(v,"0");
        if(!ex_cfg_get(cfg,"crosshair",v,24))rd_crosshair=!equal(v,"0");
    }
    rd_menu_diff=rd_difficulty;rd_sensitivity_update();
    char best[24];int b;if(pk_read_text(RD_SCORE_FILE,best,24)>=0&&!ex_parse_int(best,&b)&&b>=0)rd_best=b;
}
static void rd_save_config(void)
{
    char body[512],n[16];copy(body,"# SandRaider 1.1\ndifficulty=",512);ex_dec(n,16,rd_difficulty);append(body,n,512);
    append(body,"\nsensitivity=",512);ex_dec(n,16,rd_sensitivity);append(body,n,512);
    append(body,"\nrender=",512);ex_dec(n,16,rd_render_shift+1);append(body,n,512);
    append(body,rd_sound?"\nsound=1":"\nsound=0",512);append(body,rd_minimap?"\nminimap=1":"\nminimap=0",512);
    append(body,rd_crosshair?"\ncrosshair=1\n":"\ncrosshair=0\n",512);
    rd_setting_status=sc_write("HOME/RAIDER.CFG",body,length(body))==length(body)?1:-1;
}
