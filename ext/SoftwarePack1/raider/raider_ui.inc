static const char *rd_weapon_name(void)
{return rd_weapon==0?"PISTOL":rd_weapon==1?"SHOTGUN":rd_weapon==2?"CHAINGUN":"BLADE";}
static const char *rd_objective(void)
{return !rd_key_red?"Find the red keycard in the northwest sector":!rd_key_blue?"Open the red gate. Find the blue keycard":rd_level==5&&!rd_boss_dead?"Open the blue gate. Defeat the Warden":"Activate the green extraction switch [E]";}
static void rd_trapezoid(int x,int y,int top,int bottom,int height,u32 color)
{for(int r=0;r<height;r++){int w=top+(bottom-top)*r/height;ui_rect_rgb(x-w/2,y+r,w,1,color);}}
static void rd_draw_weapon(void)
{
    int vh=rd_view_h*100/ui_scale,cx=UI_W/2,base=vh+4;
    int kick=rd_fire_vis>0?8:0,bob=(rd_walk/12%2)*2;
    if(rd_reload)base+=18;else base+=bob+kick;
    int size=ex_clamp(vh/3,50,110);int top=base-size;
    /* 手与袖口形成稳定的前景层，枪有枪口、照门、机匣和握把。 */
    rd_trapezoid(cx-34,base-44,18,38,44,PK_WOOD);rd_trapezoid(cx+34,base-44,18,38,44,PK_WOOD);
    if(rd_weapon==3){rd_trapezoid(cx+12,top,3,18,size-30,PK_WHITE);ui_rect_rgb(cx-2,base-38,40,5,PK_GOLD);ui_rect_rgb(cx+8,base-33,16,33,PK_RUBBER);}
    else{
        int wide=rd_weapon==0?24:rd_weapon==1?42:58;
        rd_trapezoid(cx,top,wide/2,wide,size-10,PK_RUBBER);
        rd_trapezoid(cx,top+3,wide/2-4,wide-6,size-20,PK_METAL);
        ui_rect_rgb(cx-3,top+6,6,10,PK_NIGHT);ui_rect_rgb(cx-2,top+4,4,2,PK_CYAN);
        if(rd_weapon==1){ui_rect_rgb(cx-11,top+8,7,size-22,PK_NIGHT);ui_rect_rgb(cx+4,top+8,7,size-22,PK_NIGHT);rd_trapezoid(cx,base-38,34,46,22,PK_WOOD);}
        else if(rd_weapon==2){for(int i=-1;i<=1;i++)ui_rect_rgb(cx+i*12-3,top+8,6,size-30,PK_NIGHT);ui_rect_rgb(cx-25,base-38,50,14,PK_GOLD);}
        else{ui_rect_rgb(cx-7,base-36,14,36,PK_RUBBER);ui_rect_rgb(cx-9,top+20,2,size-40,PK_WHITE);}
    }
    if(rd_fire_vis&&rd_weapon<3){
        int flash=rd_weapon==1?24:16;rd_trapezoid(cx,top-flash,3,flash*2,flash,PK_ORANGE);rd_trapezoid(cx,top-flash+3,3,flash,flash-3,PK_YELLOW);ui_rect_rgb(cx-3,top-6,6,9,PK_WHITE);
    }
}
static void rd_draw_target(void)
{
    float fx=rd_cos(rd_ang),fy=rd_sin(rd_ang),best=rd_ray_dist(rd_ang);rd_enemy *target=0;
    for(int i=0;i<rd_en_count;i++){rd_enemy *e=&rd_en[i];if(!e->alive)continue;
        float dx=e->x-rd_px,dy=e->y-rd_py,t=dx*fx+dy*fy;
        if(t>0&&t<best&&rd_fabsf2(dx*fy-dy*fx)<0.35f){target=e;best=t;}}
    int cx=UI_W/2,cy=rd_view_h*100/ui_scale/2;
    if(rd_crosshair){u32 c=rd_hitmark?PK_GREEN:target?PK_RED:PK_WHITE;
        ui_rect_rgb(cx-8,cy-2,17,5,PK_NIGHT);ui_rect_rgb(cx-2,cy-8,5,17,PK_NIGHT);
        ui_rect_rgb(cx-7,cy,5,1,c);ui_rect_rgb(cx+3,cy,5,1,c);ui_rect_rgb(cx,cy-7,1,5,c);ui_rect_rgb(cx,cy+3,1,5,c);
        if(rd_hitmark){ui_rect_rgb(cx-5,cy-5,2,2,PK_GREEN);ui_rect_rgb(cx+4,cy+4,2,2,PK_GREEN);}
    }
    if(target){
        const char *name=target->type==0?"RUSHER":target->type==1?"RIFLEMAN":target->type==2?"HEAVY":target->type==3?"DRONE":"WARDEN";
        int w=ex_min(UI_W-32,180),x=(UI_W-w)/2,y=cy+22,maxhp=target->type==0?28:target->type==1?40:target->type==2?90:target->type==3?32:300;
        ui_rect_rgb(x-4,y-4,w+8,30,PK_NIGHT);pk_text(x,y,name,PK_WHITE);ui_rect_rgb(x,y+20,w,4,PK_RUBBER);ui_rect_rgb(x,y+20,w*target->hp/maxhp,4,PK_RED);
    }
}
static void rd_draw_map(void)
{
    if(!rd_minimap&&!rd_show_map)return;
    int cell=rd_show_map?ex_clamp((ex_min(UI_W,rd_view_h*100/ui_scale)-40)/24,3,12):3;
    int w=24*cell,x=rd_show_map?(UI_W-w)/2:UI_W-w-12,y=rd_show_map?(rd_view_h*100/ui_scale-w)/2:12;
    ui_rect_rgb(x-4,y-4,w+8,w+8,PK_NIGHT);pk_border(x-4,y-4,w+8,w+8,PK_MUTED);
    for(int r=0;r<24;r++)for(int c=0;c<24;c++){
        if(!rd_seen[r][c])continue;int t=rd_grid[r][c];
        u32 color=t==4?PK_RED:t==5?PK_BLUE:t==7?PK_GREEN:t?PK_METAL:PK_FLOOR;
        ui_rect_rgb(x+c*cell,y+r*cell,cell-1,cell-1,color);
    }
    for(int i=0;i<rd_it_count;i++){rd_item *it=&rd_it[i];int tx=(int)it->x,ty=(int)it->y;
        if(!it->taken&&rd_seen[ty][tx]&&(it->type==8||it->type==9))ui_rect_rgb(x+tx*cell,y+ty*cell,cell-1,cell-1,it->type==8?PK_RED:PK_BLUE);}
    ui_rect_rgb(x+(int)rd_px*cell,y+(int)rd_py*cell,cell,cell,PK_YELLOW);
    int tipx=x+(int)(rd_px*cell+rd_cos(rd_ang)*cell*2),tipy=y+(int)(rd_py*cell+rd_sin(rd_ang)*cell*2);
    ui_rect_rgb(tipx,tipy,2,2,PK_WHITE);
}
static void rd_meter(int x,int y,int w,const char *title,int value,u32 color)
{
    char label[32],n[12];copy(label,title,32);append(label," ",32);ex_dec(n,12,value);append(label,n,32);
    pk_text(x,y,label,PK_WHITE);ui_rect_rgb(x,y+20,w,5,PK_RUBBER);ui_rect_rgb(x,y+20,w*ex_clamp(value,0,100)/100,5,color);
}
static void rd_draw_hud(void)
{
    int hh=rd_hud_height(),y=UI_H-hh,cols=UI_W<560?2:4,w=(UI_W-32)/cols;
    ui_rect_rgb(0,y,UI_W,hh,PK_NIGHT);ui_rect_rgb(0,y,UI_W,1,PK_MUTED);
    rd_meter(12,y+8,w-16,"HP",rd_hp,rd_hp<26?PK_RED:PK_GREEN);rd_meter(12+w,y+8,w-16,"ARMOR",rd_armor,PK_BLUE);
    int ax=cols==2?12:12+w*2,ay=cols==2?y+42:y+8;char label[48],n[16];
    copy(label,rd_weapon==3?"AMMO --":"AMMO ",48);
    if(rd_weapon<3){ex_dec(n,16,rd_clip[rd_weapon]);append(label,n,48);append(label," / ",48);ex_dec(n,16,rd_weapon==1?rd_shells:rd_bullets);append(label,n,48);}
    pk_text(ax,ay,label,PK_GOLD);pk_text(ax,ay+19,rd_reload?"RELOADING":rd_weapon_name(),PK_CYAN);
    int kx=cols==2?12+w:12+w*3;copy(label,"L",48);ex_dec(n,16,rd_level+1);append(label,n,48);append(label,"  KILLS ",48);ex_dec(n,16,rd_kills);append(label,n,48);pk_text(kx,ay,label,PK_WHITE);
    ui_rect_rgb(kx,ay+21,12,8,rd_key_red?PK_RED:PK_RUBBER);ui_rect_rgb(kx+18,ay+21,12,8,rd_key_blue?PK_BLUE:PK_RUBBER);
    copy(label,"SCORE ",48);ex_dec(n,16,rd_score);append(label,n,48);pk_text(kx+40,ay+19,label,PK_MUTED);
    const char *hint=rd_msg_ticks>0?rd_msg:rd_objective();ui_clip_set(12,UI_H-24,UI_W-24,20);pk_text(12,UI_H-23,hint,rd_msg_ticks?PK_GOLD:PK_WHITE);ui_clip_clear();
    int mx=(int)(rd_px+rd_cos(rd_ang)*0.9f),my=(int)(rd_py+rd_sin(rd_ang)*0.9f),tile=rd_tile(mx,my);
    if(tile>=4){const char *prompt=tile==7?"[E] EXTRACT":tile==4&&!rd_key_red?"RED KEY REQUIRED":tile==5&&!rd_key_blue?"BLUE KEY REQUIRED":"[E] OPEN DOOR";int tw=ex_text_w(prompt),yy=rd_view_h*100/ui_scale/2+62;ui_rect_rgb((UI_W-tw)/2-8,yy-4,tw+16,24,PK_NIGHT);pk_text((UI_W-tw)/2,yy,prompt,PK_GOLD);}
}
static void rd_page(const char *title,const char *subtitle)
{ui_header(title,subtitle);}
static void rd_draw_menu(void)
{
    rd_page("SAND RAIDER","Six sectors / four weapons / one extraction");int y=ui_compact?40:78,x=24;
    pk_label(x,y,UI_W-48,"Breach the facility. Collect access keys. Survive.",PAL_UI_TEXT);
    char n[16],line[64];copy(line,"Best score: ",64);ex_dec(n,16,rd_best);append(line,n,64);ui_text(x,y+26,line,PAL_UI_GOLD+7);
    int dw=(UI_W-64)/3,bw=(UI_W-60)/2;
    ui_small_control(20,x,y+58,dw,"Easy",rd_difficulty==1);ui_small_control(21,x+dw+8,y+58,dw,"Normal",rd_difficulty==2);ui_small_control(22,x+2*(dw+8),y+58,dw,"Hard",rd_difficulty==3);
    ui_control(10,x,y+100,bw,UI_W<400?"Start":"Start campaign",1);ui_control(11,x+bw+12,y+100,bw,"Guide",0);
    ui_control(12,x,y+140,bw,"Settings",0);ui_control(13,x+bw+12,y+140,bw,"Exit",0);
    pk_footer("Enter start / F1 guide / F2 settings / 1-3 difficulty / Esc exit");
}
static void rd_draw_briefing(void)
{
    rd_page(rd_level_names[rd_level],"Mission briefing / equipment carries between sectors");int y=ui_compact?38:72;
    pk_label(24,y,UI_W-48,rd_briefs[rd_level],PAL_UI_TEXT);
    const char *lines[]={"1. Red key: northwest sector. E opens the red gate.","2. Blue key: southeast sector. E opens the blue gate.","3. Extraction: green switch in the northeast sector.","Search side rooms for armor, guns and treasure.","WASD move / mouse or arrows aim / click or Space fire"};
    for(int i=0;i<(UI_H<360?3:5);i++)pk_label(24,y+28+i*24,UI_W-48,lines[i],PAL_UI_MUTED);
    ui_control(30,24,UI_H-70,136,"Deploy",1);ui_control(11,172,UI_H-70,116,"Guide",0);
    pk_footer("Enter deploy / Esc menu. Death retries this sector checkpoint.");
}
/* Manual wraps at words; paging is available by buttons and arrows. */
static void rd_doc(const char **lines,int count,int y,int bottom)
{
    int columns=ex_clamp((UI_W-48)/8,12,110),row=0;rd_doc_visible=ex_max(1,(bottom-y)/20);
    ui_clip_set(24,y,UI_W-48,bottom-y);
    for(int i=0;i<count;i++){
        const char *s=lines[i];while(*s){int n=length(s);if(n>columns){n=columns;int k=n;while(k>0&&s[k]!=' ')k--;if(k>0)n=k;}
            char text[112];for(int k=0;k<n;k++)text[k]=s[k];text[n]=0;
            if(row>=rd_doc_scroll&&row<rd_doc_scroll+rd_doc_visible)ui_text(24,y+(row-rd_doc_scroll)*20,text,PAL_UI_TEXT);
            row++;s+=n;while(*s==' ')s++;
        }
    }ui_clip_clear();rd_doc_total=row;
}
static void rd_draw_guide(void)
{
    rd_page("FIELD MANUAL","Controls, enemies and objectives");int y=ui_compact?36:68;
    const char *lines[]={"W A S D move / Left Right aim / mouse capture on deploy","Left click or Space fires / R reloads / E opens or extracts","1 Pistol / 2 Shotgun / 3 Chaingun / 4 Blade (no ammo)","P or Esc pauses and releases mouse / F2 opens settings","[ and ] adjust sensitivity live / M toggles explored map","Collect red -> open red gate -> collect blue -> open blue","Green switch extracts. Last sector requires Warden kill.","Rushers close in / Riflemen shoot / Heavies absorb damage","Drones patrol / Warden carries a heavy cannon","Medkits restore HP / armor absorbs hits / crates give ammo","Kills supply bullets. Search for guns, shells and treasure."};
    rd_doc(lines,11,y,UI_H-66);
    ui_small_control(47,16,UI_H-56,96,"Back",1);ui_small_control(48,124,UI_H-56,52,"Up",0);ui_small_control(49,188,UI_H-56,60,"Down",0);
    pk_footer("Up/Down read / F1 or Esc returns");
}
static void rd_draw_settings(void)
{
    rd_page("SETTINGS","Changes apply immediately; Save persists HOME/RAIDER.CFG");int y=ui_compact?38:72;char label[64],n[16];
    copy(label,"Mouse sensitivity: ",64);ex_dec(n,16,rd_sensitivity);append(label,n,64);append(label," / 20",64);ui_text(24,y,label,PAL_UI_TEXT);
    ui_small_control(40,24,y+24,52,"-",0);ui_small_control(41,UI_W-80,y+24,52,"+",0);
    ex_progress(88,y+30,UI_W-180,10,rd_sensitivity*1000/20);
    if((ui_pressed&1)&&ui_hit(88,y+24,UI_W-180,22)){rd_sensitivity=ex_clamp(1+(ui_x-88)*20/ex_max(1,UI_W-180),1,20);rd_sensitivity_update();ui_followup=1;}
    int bw=(UI_W-60)/2;
    ui_small_control(42,24,y+54,bw,rd_sound?"Sound on":"Sound off",rd_sound);
    ui_small_control(43,36+bw,y+54,bw,rd_minimap?"Map on":"Map off",rd_minimap);
    ui_small_control(44,24,y+80,bw,rd_crosshair?"Crosshair on":"Crosshair off",rd_crosshair);
    ui_small_control(45,36+bw,y+80,bw,rd_render_shift?"Render half":"Render native",!rd_render_shift);
    ui_control(46,24,UI_H-66,104,"Save",1);ui_control(47,140,UI_H-66,104,"Back",0);
    pk_footer(rd_setting_status?(rd_setting_status>0?"Settings saved":"Write failed: check HOME permission"):"[ / ] sensitivity / Esc back");
}
static void rd_draw_pause(void)
{
    rd_page("PAUSED","Mouse released / world simulation stopped");int y=ui_compact?40:80;
    pk_label(24,y,UI_W-48,rd_objective(),PAL_UI_TEXT);
    ui_control(30,24,y+40,136,"Resume",1);ui_control(12,172,y+40,136,"Settings",0);
    ui_control(11,24,y+84,136,"Guide",0);ui_control(31,172,y+84,136,"Main menu",0);
    ui_control(13,24,y+128,136,"Exit",0);pk_footer("Enter or P resume / Esc menu. Arrow-key aiming works without capture.");
}
static void rd_draw_result(void)
{
    const char *title=rd_state==2?"SECTOR COMPLETE":rd_state==3?"YOU DIED":"MISSION CLEAR";
    rd_page(title,rd_level_names[rd_level]);int y=ui_compact?40:80;char line[96],n[16];
    copy(line,"Kills ",96);ex_dec(n,16,rd_kills);append(line,n,96);append(line," / ",96);ex_dec(n,16,rd_kill_total);append(line,n,96);ui_text(24,y,line,PAL_UI_TEXT);
    copy(line,"Supplies ",96);ex_dec(n,16,rd_items_got);append(line,n,96);append(line,"   Treasure ",96);ex_dec(n,16,rd_treasure);append(line,n,96);ui_text(24,y+28,line,PAL_UI_TEXT);
    copy(line,"Time ",96);ex_dec(n,16,rd_elapsed/100);append(line,n,96);append(line,"s   Score ",96);ex_dec(n,16,rd_score);append(line,n,96);ui_text(24,y+56,line,PAL_UI_GOLD+7);
    ui_control(32,24,y+104,192,rd_state==3?"Retry checkpoint":rd_state==4?"Main menu":"Next sector",1);
    ui_control(13,228,y+104,96,"Exit",0);
    pk_footer(rd_state==3?"R or Enter retries without duplicating score / Esc menu":"Enter continues / Esc menu");
}
