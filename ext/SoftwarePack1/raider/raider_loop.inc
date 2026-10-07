static void rd_open_page(int page)
{
    rd_resume_state=rd_state==1?5:rd_state;
    rd_release_mouse();rd_state=page;rd_setting_status=0;rd_doc_scroll=0;ui_followup=1;
}
static void rd_adjust_sens(int delta)
{
    rd_sensitivity+=delta;rd_sensitivity_update();char msg[48],n[16];
    copy(msg,"Sensitivity ",48);ex_dec(n,16,rd_sensitivity);append(msg,n,48);append(msg," / 20",48);rd_msg_set(msg);ui_followup=1;
}
static int rd_action(int id)
{
    if(id==PK_ABOUT_ID){if(rd_state==1)rd_pause();else rd_release_mouse();pk_about("SandRaider","Six-sector action game");return 0;}
    switch(id){
    case 10:rd_start_game();break;
    case 11:rd_open_page(6);break;case 12:rd_open_page(7);break;case 13:rd_release_mouse();return 1;
    case 20:case 21:case 22:rd_difficulty=rd_menu_diff=id-19;break;
    case 30:rd_state=1;rd_try_capture();rd_discover();rd_msg_set(rd_mouse_captured?"Mouse captured. Esc pauses / F1 guide":"Mouse capture unavailable: use arrows to aim.");break;
    case 31:rd_release_mouse();rd_state=0;break;
    case 32:if(rd_state==3)rd_retry();else if(rd_state==2)rd_next_level();else rd_state=0;break;
    case 40:rd_adjust_sens(-1);break;case 41:rd_adjust_sens(1);break;
    case 42:rd_sound=!rd_sound;if(rd_sound)exaudio_init();else exaudio_shutdown();break;
    case 43:rd_minimap=!rd_minimap;break;case 44:rd_crosshair=!rd_crosshair;break;
    case 45:rd_render_shift=!rd_render_shift;break;case 46:rd_save_config();break;
    case 47:rd_state=rd_resume_state;break;
    case 48:rd_doc_scroll=ex_max(0,rd_doc_scroll-rd_doc_visible);break;
    case 49:rd_doc_scroll=ex_min(ex_max(0,rd_doc_total-rd_doc_visible),rd_doc_scroll+rd_doc_visible);break;
    }ui_followup=1;return 0;
}
static int rd_key_event(int key)
{
    if(key<0)return 0;
    if(key=='['){rd_adjust_sens(-1);return 0;}if(key==']'){rd_adjust_sens(1);return 0;}
    if(key==1){if(rd_state==6)rd_state=rd_resume_state;else if(rd_state!=7)rd_open_page(6);return 0;}
    if(key==2){if(rd_state==7)rd_state=rd_resume_state;else if(rd_state!=6)rd_open_page(7);return 0;}
    switch(rd_state){
    case 0:if(key>='1'&&key<='3')rd_difficulty=rd_menu_diff=key-'0';else if(key==10)rd_start_game();else if(key==27)return 1;break;
    case 1:
        if(key==27||key=='p'||key=='P')rd_pause();
        else if(key==' ')rd_fire();else if(key=='r'||key=='R')rd_reload_begin();
        else if(key=='e'||key=='E')rd_use();else if(key>='1'&&key<='4')rd_switch_weapon(key-'1');
        else if(key=='m'||key=='M')rd_show_map=!rd_show_map;break;
    case 2:if(key==10||key==' ')rd_next_level();else if(key==27)rd_state=0;break;
    case 3:if(key=='r'||key=='R'||key==10)rd_retry();else if(key==27)rd_state=0;break;
    case 4:if(key=='r'||key=='R'||key==10||key==27)rd_state=0;break;
    case 5:if(key==10||key=='p'||key=='P')rd_action(30);else if(key==27)rd_state=0;break;
    case 6:if(key==27)rd_state=rd_resume_state;else if(key==0x80)rd_action(48);else if(key==0x81)rd_action(49);break;
    case 7:if(key==27)rd_state=rd_resume_state;break;
    case 8:if(key==10)rd_action(30);else if(key==27)rd_state=0;break;
    }ui_followup=1;return 0;
}
int main(void)
{
    if(ui_open("SandRaider / SandCore ext")<0)return 1;
    rd_trig_init();rd_gen_textures();rd_gen_sprites();rd_load_config();ex_rand_seed((u32)sc_tick());
    if(rd_sound)exaudio_init();int last=sc_tick();ui_followup=1;
    for(;;){
        if(!ui_frame_due_interval(rd_state==1?4:0))continue;
        ui_pointer();rd_geometry();int now=sc_tick(),dt=ex_clamp(now-last,0,20);last=now;
        int key=sc_key(),before=rd_state,lost_capture=0;
        if(rd_state==1){
            if(!ui_focus){rd_pause();lost_capture=1;}
            else if(rd_mouse_captured){
                int snap[8];
                if(sc_mouse_relative(ui_win,snap)||!snap[6]||!snap[7]){rd_pause();lost_capture=1;}
                else{rd_ang+=(float)snap[1]*rd_sens;rd_mouse_fire=(snap[3]&1)||(snap[4]&1);if(snap[4]&1)rd_fire();}
            }else rd_mouse_fire=(ui_buttons&1)&&ui_y<rd_view_h*100/ui_scale;
        }
        /* Esc 已被窗口管理器释放捕获。这一帧只打开暂停，不能把
         * 同一个 Esc 再解释成退出暂停而直接丢掉整关进度。 */
        if(lost_capture&&key==27)key=-1;
        if(rd_key_event(key)){rd_release_mouse();exaudio_shutdown();return 0;}
        if(before!=rd_state)dt=0;
        for(int i=0;i<dt&&rd_state==1;i++)rd_sim();
        switch(rd_state){
        case 0:rd_draw_menu();break;
        case 1:
            if(rd_ensure_buffer()){rd_pause();rd_msg_set("Not enough memory. Choose half render in Settings.");rd_draw_pause();break;}
            rd_render_world();rd_blit();rd_draw_weapon();rd_draw_target();rd_draw_map();rd_draw_hud();break;
        case 2:case 3:case 4:rd_draw_result();break;case 5:rd_draw_pause();break;
        case 6:rd_draw_guide();break;case 7:rd_draw_settings();break;case 8:rd_draw_briefing();break;
        }
        ui_present();before=rd_state;
        if(ui_action==PK_ABOUT_ID){rd_action(PK_ABOUT_ID);last=sc_tick();continue;}
        if(ui_action&&rd_action(ui_action)){rd_release_mouse();exaudio_shutdown();return 0;}
        if(before!=rd_state)last=sc_tick();exaudio_pump();
    }
}
