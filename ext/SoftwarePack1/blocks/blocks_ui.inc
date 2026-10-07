/* 所有场地、预览、覆盖层共用这一组几何，杜绝固定像素与缩放混用。 */
static void bk_geometry(void)
{
    bk_side=UI_W>=520?120:92;
    bk_fy=ui_compact?40:68;
    int available=UI_H-bk_fy-(ui_compact?22:30)-12;
    bk_cell=ex_clamp(ex_min(available/BK_H,(UI_W-2*bk_side-48)/BK_W),4,28);
    bk_fx=(UI_W-BK_W*bk_cell)/2;
}
static void bk_tile(int x,int y,int size,u32 rgb)
{
    ui_rect_rgb(x,y,size-1,size-1,PK_INK);
    if(size>3){ui_rect_rgb(x+1,y+1,size-3,size-3,rgb);ui_rect_rgb(x+1,y+1,size-3,1,PK_WHITE);}
}
static void bk_draw_cell(int x,int y,u32 rgb)
{bk_tile(bk_fx+x*bk_cell,bk_fy+y*bk_cell,bk_cell,rgb);}
static void bk_draw_piece(int px,int py,int piece,int rot,u32 rgb)
{for(int i=0;i<4;i++){int x=px+bk_shapes[piece][rot][i][0],y=py+bk_shapes[piece][rot][i][1];if(y>=0&&y<BK_H)bk_draw_cell(x,y,rgb);}}
static void bk_draw_mini(int x,int y,int w,int h,int piece)
{
    if(piece<0)return;
    int minx=9,miny=9,maxx=0,maxy=0;
    for(int i=0;i<4;i++){int a=bk_shapes[piece][0][i][0],b=bk_shapes[piece][0][i][1];minx=ex_min(minx,a);miny=ex_min(miny,b);maxx=ex_max(maxx,a);maxy=ex_max(maxy,b);}
    int cell=ex_clamp(ex_min((w-12)/(maxx-minx+1),(h-4)/(maxy-miny+1)),4,16);
    int ox=x+(w-(maxx-minx+1)*cell)/2,oy=y+(h-(maxy-miny+1)*cell)/2;
    for(int i=0;i<4;i++)bk_tile(ox+(bk_shapes[piece][0][i][0]-minx)*cell,oy+(bk_shapes[piece][0][i][1]-miny)*cell,cell,bk_colors[piece]);
}
static void bk_overlay(const char *title,const char *detail)
{
    int w=ex_min(UI_W-32,440),h=bk_state==3?126:104,x=(UI_W-w)/2,y=(UI_H-h)/2;
    ui_panel(x,y,w,h);pk_label(x+12,y+12,w-24,title,PAL_UI_CYAN+7);
    pk_label(x+12,y+38,w-24,detail,PAL_UI_TEXT);
    if(bk_state==3){ui_rect(x+12,y+64,w-24,24,PAL_UI_INK);ui_text(x+20,y+68,bk_name[0]?bk_name:"PLAYER",PAL_UI_TEXT);}
    ui_small_control(1,x+12,y+h-28,100,bk_state==3?"Save name":bk_state==2?"Restart":"Resume",1);
    if(bk_state!=3)ui_small_control(2,x+w-116,y+h-28,104,"New game",0);
}
static void bk_draw(void)
{
    ui_header("SandBlocks","7-bag / hold / combos / F1 help");bk_geometry();
    int fw=BK_W*bk_cell,fh=BK_H*bk_cell,lx=bk_fx-bk_side-12,rx=bk_fx+fw+12;
    ui_rect_rgb(bk_fx-3,bk_fy-3,fw+6,fh+6,PK_METAL);
    ui_rect_rgb(bk_fx,bk_fy,fw,fh,PK_NIGHT);
    for(int y=0;y<BK_H;y++)for(int x=0;x<BK_W;x++){
        if(bk_grid[y][x])bk_draw_cell(x,y,bk_colors[bk_grid[y][x]-1]);
        else{ui_rect_rgb(bk_fx+x*bk_cell+bk_cell-1,bk_fy+y*bk_cell,1,bk_cell,PK_FLOOR);ui_rect_rgb(bk_fx+x*bk_cell,bk_fy+y*bk_cell+bk_cell-1,bk_cell,1,PK_FLOOR);}
    }
    if(bk_state==0||bk_state==1){
        int gy;bk_ghost_y(&gy);
        if(bk_ghost&&gy!=bk_py)for(int i=0;i<4;i++){
            int x=bk_px+bk_shapes[bk_piece][bk_rot][i][0],y=gy+bk_shapes[bk_piece][bk_rot][i][1];
            if(y>=0)pk_border(bk_fx+x*bk_cell+1,bk_fy+y*bk_cell+1,bk_cell-3,bk_cell-3,PK_MUTED);}
        bk_draw_piece(bk_px,bk_py,bk_piece,bk_rot,bk_colors[bk_piece]);
    }
    ui_panel(lx,bk_fy,bk_side,126);char n[16];
    ui_text(lx+8,bk_fy+8,"SCORE",PAL_UI_MUTED);ex_dec(n,16,bk_score);ui_text(lx+8,bk_fy+28,n,PAL_UI_TEXT);
    ui_text(lx+8,bk_fy+52,"LINES",PAL_UI_MUTED);ex_dec(n,16,bk_lines);ui_text(lx+8,bk_fy+70,n,PAL_UI_TEXT);
    ui_text(lx+8,bk_fy+92,"LEVEL",PAL_UI_MUTED);ex_dec(n,16,bk_level);ui_text(lx+8,bk_fy+110,n,PAL_UI_TEXT);
    int preview_h=ex_min(54,ex_max(24,(fh-100)/3));
    ui_panel(rx,bk_fy,bk_side,70);ui_text(rx+8,bk_fy+6,bk_hold_used?"HOLD *":"HOLD",PAL_UI_MUTED);
    ui_rect_rgb(rx+4,bk_fy+26,bk_side-8,40,PK_NIGHT);bk_draw_mini(rx+4,bk_fy+26,bk_side-8,40,bk_hold);
    ui_panel(rx,bk_fy+78,bk_side,26+3*preview_h);ui_text(rx+8,bk_fy+84,"NEXT",PAL_UI_MUTED);
    for(int i=0;i<3;i++){int yy=bk_fy+102+i*preview_h;ui_rect_rgb(rx+4,yy,bk_side-8,preview_h-2,PK_NIGHT);bk_draw_mini(rx+4,yy,bk_side-8,preview_h-2,bk_next[i]);}
    if(fh>250){ui_text(lx+8,bk_fy+142,"BEST",PAL_UI_MUTED);ex_udec(n,16,bk_hs_score[0]);ui_text(lx+8,bk_fy+162,n,PAL_UI_TEXT);}
    if(fh>300){pk_label(lx+8,bk_fy+194,bk_side-16,bk_event,PAL_UI_CYAN+7);if(bk_combo>0){ex_dec(n,16,bk_combo+1);ui_text(lx+8,bk_fy+216,"COMBO",PAL_UI_MUTED);ui_text(lx+8,bk_fy+236,n,PAL_UI_TEXT);}}
    pk_footer("Arrows move / Up,Z rotate / Space drop / C hold / P pause / F1 help");
    if(bk_state==1)bk_overlay("PAUSED","P or Enter resumes / F1 guide");
    else if(bk_state==2)bk_overlay("GAME OVER","R starts again / Esc exits");
    else if(bk_state==3)bk_overlay("NEW HIGH SCORE","Enter your name (11 characters)");
    if(bk_help){
        int w=ex_min(UI_W-32,500),h=ex_min(UI_H-40,270),x=(UI_W-w)/2,y=(UI_H-h)/2;ui_panel(x,y,w,h);
        const char *lines[]={"HOW TO PLAY / F1 or Esc closes","Complete rows; four rows award a bonus","C holds once per piece / Up or Z rotates","Down soft drop / Space drops to the outline","P pauses / R restarts / focus loss pauses","Repeated clears build combos and four-line chains"};
        ui_clip_set(x+8,y+8,w-16,h-16);for(int i=0;i<6;i++)pk_label(x+12,y+12+i*24,w-24,lines[i],i?PAL_UI_TEXT:PAL_UI_CYAN+7);
        int yy=y+164;ui_text(x+12,yy,"HIGH SCORES",PAL_UI_MUTED);
        for(int i=0;i<3;i++){char row[64],score[16];ex_udec(score,16,bk_hs_score[i]);copy(row,bk_hs_name[i][0]?bk_hs_name[i]:"---",64);append(row,"  ",64);append(row,score,64);ui_text(x+12,yy+22+i*20,row,PAL_UI_TEXT);}ui_clip_clear();
        ui_action=0;
    }
}
