/* mio：玩法界面独立于世界射线。每页最多使用可见行数，所有页
 * 都能鼠标翻页；小窗口不隐藏最后一项配方/遗迹/库存。模态面板
 * 打开后只生成面板按钮，不能让旧世界按钮透过面板继续消耗物品。 */
static void world_panel_open(int panel)
{world_panel=panel;world_page=0;world_dragging=world_captured=0;sc_mouse_capture(ui_win,0);}
static void world_select_mode(int mode)
{
    if(mode<1||mode>2)return;
    if(mode==world_mode){world_panel_open(4);return;}
    if(changed){world_pending_mode=mode;world_panel_open(5);return;}
    world_mode=mode;world_new_generate();world_new_load();world_panel_open(4);
}
static void world_game_key(int key)
{
    if(world_mode==0)return;
    if(key=='b'||key=='B')world_panel_open(world_panel==1?0:1);
    if(key=='j'||key=='J')world_panel_open(world_panel==2?0:2);
    if(key=='i'||key=='I')world_panel_open(world_panel==3?0:3);
    if(key=='m'||key=='M')world_panel_open(world_panel==6?0:6);
    if(key=='p'||key=='P')world_panel_open(world_panel==4?0:4);
    if(world_panel){
        if(key==9)world_page++;
        if(world_panel==1){
            if(key==0x80)world_recipe=(world_recipe+8)%9;
            if(key==0x81)world_recipe=(world_recipe+1)%9;
            if(key==0x80||key==0x81){
                int rows=(ui_clamp(UI_H-20,150,440)-110)/28;if(rows<1)rows=1;world_page=world_recipe/rows;
            }
            if(key==10)world_craft(world_recipe);
        }
        return;
    }
    if(key=='r'||key=='R'){
        if(world_elapsed<world_recall_cooldown)copy(message,"Recall cooling down",sizeof(message));
        else{world_recall_tick=world_elapsed?world_elapsed:1;world_recall_x=px;world_recall_z=pz;copy(message,"Recall / hold still 2.5 seconds",sizeof(message));}
    }
    if(key>='1'&&key<='9'){world_slot=key-'1';material=hotbar[world_slot];changed=1;}
}
static void world_game_action(int action)
{
    if(action==40)world_panel_open(1);if(action==41)world_panel_open(2);
    if(action==42)world_panel_open(3);if(action==43)world_panel_open(0);
    if(action==44)world_page++;if(action==45){if(world_page)world_page--;}
    if(action==46)world_panel_open(6);if(action==47)world_panel_open(4);
    if(action>=50&&action<=58){world_slot=action-50;material=hotbar[world_slot];changed=1;}
    if(action>=100&&action<109){world_recipe=action-100;world_craft(world_recipe);}
    if(action>=120&&action<136){
        int kind=action-119;
        if(kind==13||kind==14||kind==16){copy(message,"This item is not a building block",sizeof(message));return;}
        hotbar[world_slot]=material=kind;changed=1;world_panel_open(0);
    }
    if(action==200)world_panel_open(0);
    if(action==201)save_world();
    if(action==202){if(!load_world())copy(message,"No valid save / world kept",sizeof(message));}
    if(action==203)world_select_mode(world_mode==1?2:1);
    if(action==204){world_pending_mode=world_mode;world_panel_open(7);}
    if(action==205){if(changed){world_pending_mode=0;world_panel_open(5);}else world_exit=1;}
    /* 捕获时鼠标是视角，不能同时点击HUD。把制作/背包/手册/地图
     * 也放在Esc释放后的暂停菜单，鼠标玩家不会只能看见HUD入口，
     * 却必须先记住B/I/J/M才能打开它们。所有入口仍共用同一面板。 */
    if(action==206)world_panel_open(1);if(action==207)world_panel_open(3);
    if(action==208)world_panel_open(2);if(action==209)world_panel_open(6);
    if(action==210){
        if(world_panel==5){save_world();if(changed)return;}
        if(world_pending_mode==0){world_exit=1;return;}
        world_mode=world_pending_mode;world_new_generate();
        if(world_panel==5)world_new_load();world_panel_open(4);
    }
    if(action==211){
        if(world_panel==7){world_panel_open(4);return;}
        if(!world_pending_mode){world_exit=1;return;}
        world_mode=world_pending_mode;world_new_generate();
        if(world_panel==5)world_new_load();world_panel_open(4);
    }
}
static void world_map(int x,int y,int width,int height)
{
    int size=width<height?width:height;if(size<32)return;
    int ox=x+(width-size)/2,oz=y+(height-size)/2;
    /* 地图投影从真实体素顶面读取；玩家建造与拆除后随之变化。
     * 仅地图允许一格代表多个体素，第一人称仍逐物理像素求交。 */
    for(int z=0;z<64;z++)for(int xx=0;xx<64;xx++){
        int top=world_top(xx,z),n=cell(xx,top,z),kind=world[n];
        u32 color=kind==7?SCN_SAND:kind==11?SCN_OAK:kind==3?SCN_STONE:SCN_GRASS;
        if(top<6)color=SCN_SEA;
        int left=ox+xx*size/64,right=ox+(xx+1)*size/64;
        int a=oz+z*size/64,b=oz+(z+1)*size/64;
        ui_rect_rgb(left,a,right-left,b-a,scn_light(color,210+top*3));
    }
    for(int i=0;i<4;i++)scn_disc(ox+site_x[i]*size/64,oz+site_z[i]*size/64,3,
        world_sites&(1<<i)?SCN_GLASS:SCN_SUN);
    scn_disc(ox+12*size/64,oz+18*size/64,3,SCN_IVORY);
    scn_disc(ox+(px>>8)*size/64,oz+(pz>>8)*size/64,3,SCN_PAINT);
}
static void world_overlay(void)
{
    if(world_mode==0)return;
    char text[96],number[16];int next=world_next_site();
    if(!world_panel){
        int short_hud=UI_H<240;
        /* HP面板只用于能容纳它的高度。160高的客户区若继续
         * 叠同一面板，会覆盖整个第一人称视口；矮窗口把HP/昼夜
         * 移到标题栏，并缩短快捷栏，核心操作仍全部鼠标可访问。 */
        if(!short_hud){
            ui_panel(8,ui_compact?26:38,UI_W>500?270:UI_W-16,42);
            copy(text,"HP ",sizeof(text));decimal(number,world_health);append(text,number,sizeof(text));
            append(text," / tool ",sizeof(text));decimal(number,world_tool);append(text,number,sizeof(text));
            append(text,world_sun_phase>=7&&world_sun_phase<=10?" / NIGHT":" / DAY",sizeof(text));
            ui_text(16,ui_compact?30:42,text,PAL_UI_TEXT);
            copy(text,"Stamina ",sizeof(text));decimal(number,world_stamina/10);append(text,number,sizeof(text));
            append(text," / air ",sizeof(text));decimal(number,world_breath/10);append(text,number,sizeof(text));
            ui_text(16,ui_compact?50:62,text,PAL_UI_MUTED);
        }
        if(world_recall_tick){
            ui_panel(UI_W/2-96,UI_H/2-20,192,36);ui_text(UI_W/2-84,UI_H/2-12,"RECALL / HOLD STILL",PAL_UI_CYAN+7);
        }
        if(UI_W>500 && !short_hud){
            ui_panel(UI_W-198,38,190,66);ui_text(UI_W-190,44,next<0?"DAWN RESTORED":site_names[next],PAL_UI_TEXT);
            if(next>=0){
                copy(text,"X ",sizeof(text));decimal(number,site_x[next]);append(text,number,sizeof(text));
                append(text," Z ",sizeof(text));decimal(number,site_z[next]);append(text,number,sizeof(text));
                append(text," cores ",sizeof(text));decimal(number,site_cost[next]);append(text,number,sizeof(text));
                ui_text(UI_W-190,66,text,PAL_UI_MUTED);
            }
            ui_text(UI_W-190,84,"J journal / M map",PAL_UI_CYAN+7);
        }
        int y=UI_H-(short_hud?70:94);ui_rect_rgb(0,y-2,UI_W,UI_H-y+2,ui_role(SC_THEME_PAPER));
        int step=(UI_W-16)/9;
        for(int i=0;i<9;i++){
            char label[4];decimal(label,i+1);ui_small_control(50+i,8+i*step,y,step-3,label,world_slot==i);
        }
        if(!short_hud){
            ui_text(8,y+26,item_names[material],PAL_UI_TEXT);
            if(UI_W>430)ui_number(144,y+26,world_mode==2?999:inventory[material],PAL_UI_MUTED);
        }
        const char *labels[]={"Craft","Items","Map","Menu"};int ids[4]={40,42,46,47};
        for(int i=0;i<4;i++)ui_small_control(ids[i],8+i*(UI_W-16)/4,y+(short_hud?26:46),(UI_W-16)/4-4,labels[i],0);
        ui_footer(message);return;
    }
    ui_action=0;
    int width=ui_clamp(UI_W-20,220,560),height=ui_clamp(UI_H-20,150,440);
    /* 制作页为配方材料独立预留24单位，窄窗也能知道需要多少。
     * 键盘选中行的分页计算使用同一个110保留量，不会把选择滚到
     * 已不可见的下一页；其它页仍使用原86单位表头/导航保留区。 */
    int x=(UI_W-width)/2,top=(UI_H-height)/2,body=height-(world_panel==1?110:86),rows=body/28;
    if(rows<1)rows=1;
    ui_panel(x,top,width,height);
    const char *titles[7]={"CRAFTING","JOURNAL","INVENTORY","PAUSED / WORLD","UNSAVED WORLD","MAP / YOUR ROUTE","NEW WORLD"};
    ui_text(x+12,top+10,titles[world_panel-1],PAL_UI_TEXT);
    ui_small_control(43,x+width-70,top+8,58,"Close",0);
    ui_clip_set(x+8,top+36,width-16,body+4);
    if(world_panel==1){
        int pages=(9+rows-1)/rows;world_page%=pages;
        for(int n=0;n<rows&&world_page*rows+n<9;n++){
            int r=world_page*rows+n;ui_small_control(100+r,x+12,top+38+n*28,width-24,recipe_names[r],r==world_recipe);
        }
    }else if(world_panel==2){
        const char *help[8]={"Logs -> planks -> stone tools","Iron tools unlock crystals","Coal + iron + crystal -> core",
            "Right-click beacons in order","Torch / campfire provide shelter","Night shades drop crystals",
            "R: hold still to return to camp","All beacons repaired: build freely"};
        int pages=(12+rows-1)/rows;world_page%=pages;
        for(int n=0;n<rows&&world_page*rows+n<12;n++){
            int i=world_page*rows+n;
            if(i<8)ui_text(x+12,top+40+n*28,help[i],PAL_UI_TEXT);
            else{
                int site=i-8;copy(text,world_sites&(1<<site)?"OK  ":"--  ",sizeof(text));append(text,site_names[site],sizeof(text));
                append(text," / cores ",sizeof(text));decimal(number,site_cost[site]);append(text,number,sizeof(text));
                ui_text(x+12,top+40+n*28,text,PAL_UI_TEXT);
            }
        }
    }else if(world_panel==3){
        int pages=(16+rows-1)/rows;world_page%=pages;
        for(int n=0;n<rows&&world_page*rows+n<16;n++){
            int kind=world_page*rows+n+1;copy(text,item_names[kind],sizeof(text));append(text,"  ",sizeof(text));
            decimal(number,inventory[kind]);append(text,number,sizeof(text));
            ui_small_control(119+kind,x+12,top+38+n*28,width-24,text,material==kind);
        }
    }else if(world_panel==4){
        const char *labels[10]={"Resume / capture mouse","Save world","Load saved world",world_mode==1?"Open creative world":"Open expedition",
            "New world / confirm","Exit / check unsaved","Crafting","Inventory / hotbar","Journal / objectives","Map / route"};
        int pages=(10+rows-1)/rows;world_page%=pages;
        for(int n=0;n<rows&&world_page*rows+n<10;n++){
            int i=world_page*rows+n;ui_small_control(200+i,x+12,top+38+n*28,width-24,labels[i],0);
        }
    }else if(world_panel==5||world_panel==7){
        ui_text(x+12,top+40,world_panel==5?"Save before leaving this world?":"Replace current in-memory world?",PAL_UI_TEXT);
        if(rows>1)ui_small_control(210,x+12,top+66,width-24,world_panel==5?"Save and continue":"Confirm new world",0);
        if(rows>2&&world_panel==5)ui_small_control(211,x+12,top+94,width-24,"Discard and continue",0);
    }else if(world_panel==6)world_map(x+12,top+38,width-24,body);
    ui_clip_clear();
    if(world_panel==1)ui_text(x+12,top+height-66,recipe_details[world_recipe],PAL_UI_MUTED);
    if(world_panel==5||world_panel==7){
        ui_small_control(210,x+12,top+height-42,(width-30)/2,world_panel==5?"Save / Go":"Confirm",0);
        ui_small_control(211,x+18+(width-30)/2,top+height-42,(width-30)/2,world_panel==5?"Discard":"Cancel",0);
    }else{
        ui_small_control(45,x+12,top+height-42,58,"Prev",0);ui_small_control(44,x+76,top+height-42,58,"Next",0);
        if(width>380 && world_panel!=1)ui_text(x+144,top+height-40,message,PAL_UI_MUTED);
    }
    /* 模态制作失败也必须可见。独立页脚位于导航按钮下方，矮窗
     * 仅覆盖面板底部留白；材料行保留消耗，页脚报告缺料/满库存/
     * 保存失败，不能让一次明确拒绝在画面上看起来没有任何反馈。 */
    ui_footer(message);
}
