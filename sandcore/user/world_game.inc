/* =====================================================================
 * mio：SAND WORLD / Dawn Expedition（黎明远征）玩法。
 *
 * 参考miomc的内容组织，采用自己的小型世界和整数实现。四座信标
 * 串联“原木->木板->石工具->铁工具->核心”链；营地/已修复信标
 * 提供安全区。每个世界确定性生成，关键材料有保证，不靠宿主
 * 磁盘里预置完成任务或按文件名返回胜利。玩家采集、消耗、建造、
 * 受伤、恢复与远征进度都在本普通三环任务执行。
 *
 * 远征/创造分别写EXPEDITION.SCW与CREATIVE.SCW，legacy才读旧CHUNK。
 * 失败存档不清dirty，读入先完整验证再提交，坏存档不覆盖当前世界。
 * 长期状态使用明确小端字段，不把C结构体填充字节当磁盘格式。
 * ===================================================================== */
static int world_panel=4,world_recipe,world_seed=20261003;
static int world_page,world_pending_mode,world_stamina=1000,world_breath=1000,world_kills;
static int world_exit;
static int world_daylight=256;
static const char *world_save_path(void)
{return world_mode==2?"HOME/WORLD/CREATIVE.SCW":"HOME/WORLD/EXPEDITION.SCW";}
static int inventory[17],world_tool,world_health=20,world_sites,world_elapsed;
static int world_deaths,world_mined,world_built,world_heal_tick,world_hurt_tick;
static int world_last_damage=-100;
static int world_recall_tick,world_recall_x,world_recall_z,world_recall_cooldown;
static int world_sun_phase,world_lighting_generation=1;
static int hotbar[9]={1,3,5,11,4,12,15,7,6},world_slot;
static const char *item_names[17]={"Air","Grass","Soil","Stone","Glass","Log","Leaves","Sand",
    "Coal","Iron","Crystal","Planks","Torch","Beacon","Core","Campfire","Water"};
static const char *recipe_names[9]={"Planks x4","Stone tools","Iron tools","Core x1","Glass x4",
    "Torches x4","Campfire x1","Herbal healing","Stone x4"};
static const char *recipe_details[9]={"Log1","Planks4 Stone6","Stone4 Iron4",
    "Coal2 Iron1 Crystal1","Sand4 Coal1","Planks1 Coal1",
    "Log2 Stone4","Leaves2 / heal4","Soil4 Sand1"};
static int site_x[4]={18,48,48,22},site_z[4]={47,47,17,12},site_cost[4]={1,1,2,3};
static int site_y[4];
static const char *site_names[4]={"Forest Echo","Tidal Court","Stone Watch","Dawn Gate"};

static int world_top(int x,int z)
{
    for(int y=world_height-2;y>0;y--){int i=cell(x,y,z);if(i>=0 && world[i] && world[i]!=16 && world[i]!=6)return y;}
    return 1;
}
static void world_put(int x,int y,int z,int kind)
{int index=cell(x,y,z);if(index>=0)world[index]=(u8)kind;}
static int world_near(int x,int z,int radius)
{return ui_abs(px-x*256-128)+ui_abs(pz-z*256-128)<radius*256;}
static int world_safe(void)
{
    if(world_near(12,18,9))return 1;
    for(int i=0;i<4;i++)if((world_sites&(1<<i)) && world_near(site_x[i],site_z[i],8))return 1;
    /* 建营火是有用途的建造：玩家可以在路线中建立自己的安全点，
     * 查询只扫附近7x7x4单元，不遍历整张世界。 */
    for(int y=(py>>8)-3;y<=(py>>8);y++)for(int z=(pz>>8)-3;z<=(pz>>8)+3;z++)
        for(int x=(px>>8)-3;x<=(px>>8)+3;x++){int n=cell(x,y,z);if(n>=0 && (world[n]==15||world[n]==12))return 1;}
    return 0;
}
static int world_next_site(void)
{for(int i=0;i<4;i++)if(!(world_sites&(1<<i)))return i;return -1;}
static void world_spawn(void)
{px=12*256+128;pz=18*256+128;py=9*256+160;yaw=512;pitch=-10;jump_velocity=0;}
static void world_new_generate(void)
{
    for(int y=0;y<world_height;y++)for(int z=0;z<world_depth;z++)for(int x=0;x<world_width;x++){
        int h=7+scn_sin(x*18+world_seed)*3/256+scn_cos(z*16)*2/256;
        if(x>35 && z>29)h-=3; /* 潮汐低地；西部树林、北部高地各有路线。 */
        h=ui_clamp(h,3,13);int kind=0;
        if(y<h){kind=y==0?3:y<h-3?3:y==h-1?(x>35?7:1):2;
            u32 ore=scn_hash(x,y,z+world_seed);
            if(y>0 && y<h-2 && ore%37==0)kind=8;
            if(y>0 && y<h-3 && ore%61==0)kind=9;
            if(y>0 && y<h-3 && ore%101==0)kind=10;
            if(y>1 && y<h-3 && ((x-32)*(x-32)+(z-32)*(z-32)<80) && ore%5<2)kind=0;
        }else if(y<6)kind=16;
        world[cell(x,y,z)]=(u8)kind;
    }
    for(int z=5;z<world_depth-5;z+=8)for(int x=5;x<world_width-5;x+=9){
        if(x>35 && z>29)continue;int h=world_top(x,z);
        for(int y=1;y<=4;y++)world_put(x,h+y,z,5);
        for(int dz=-2;dz<=2;dz++)for(int dx=-2;dx<=2;dx++)
            if(ui_abs(dx)+ui_abs(dz)<4)world_put(x+dx,h+4,z+dz,6);
        world_put(x,h+5,z,6);
    }
    /* 出生营地清理到固定高程，保证石/木/煤/铁/晶体链都可实际获得。
     * 保证的只是原材料，不预置工具、核心或已完成信标。 */
    for(int z=14;z<=22;z++)for(int x=8;x<=16;x++){
        for(int y=1;y<world_height;y++)world_put(x,y,z,y<7?3:y==7?11:0);
    }
    world_put(12,8,16,15);
    for(int x=9;x<=11;x++)for(int y=8;y<=11;y++)world_put(x,y,24,5);
    for(int z=20;z<=28;z++){
        world_put(19,7,z,3);world_put(18,8,z,8);world_put(19,8,z,8);world_put(20,8,z,9);world_put(20,9,z,9);world_put(21,8,z,10);
    }
    for(int i=0;i<4;i++){
        int h=world_top(site_x[i],site_z[i]);site_y[i]=ui_clamp(h+1,7,15);
        for(int dz=-3;dz<=3;dz++)for(int dx=-3;dx<=3;dx++){
            for(int y=1;y<=site_y[i];y++)world_put(site_x[i]+dx,y,site_z[i]+dz,3);
            for(int y=site_y[i]+1;y<site_y[i]+6;y++)world_put(site_x[i]+dx,y,site_z[i]+dz,0);
        }
        for(int side=-1;side<=1;side+=2)for(int y=1;y<=3;y++)
            world_put(site_x[i]+side*3,site_y[i]+y,site_z[i],3);
        world_put(site_x[i],site_y[i]+1,site_z[i],13);
    }
    /* 每个遗迹有三格宽的阶梯，地面再高也能靠跳跃抵达。
     * 路线不能只在地图上存在、实际留下不可攀的垂直石墙。 */
    for(int i=0;i<4;i++)for(int step=0;step<8;step++)for(int lane=-1;lane<=1;lane++){
        int z=site_z[i]+4+step,h=site_y[i]-step;
        if(h<1)h=1;
        for(int y=1;y<world_height;y++)world_put(site_x[i]+lane,y,z,y<=h?3:0);
    }
    /* 中部矿洞采用连续通道与支柱，不使用互不连通的随机孔洞。
     * 入口落在采矿区，三条支廊让地下也有方向与额外资源奖励。 */
    for(int z=25;z<43;z++)for(int x=29;x<=33;x++){
        for(int y=2;y<=4;y++)world_put(x,y,z,0);
        if(z%5==0){world_put(29,2,z,5);world_put(29,3,z,5);world_put(33,2,z,5);world_put(33,3,z,5);}
        if(z%4==0){world_put(30,1,z,8);world_put(32,1,z,9);}
    }
    for(int step=0;step<8;step++)for(int lane=0;lane<3;lane++){
        int x=23+step,z=25+lane,h=ui_clamp(8-step,1,8);
        for(int y=1;y<world_height;y++)world_put(x,y,z,y<=h?3:0);
    }
    for(int i=0;i<17;i++)inventory[i]=0;
    world_tool=world_sites=world_elapsed=world_deaths=world_mined=world_built=0;
    world_health=20;world_stamina=world_breath=1000;world_kills=0;world_sun_phase=0;world_daylight=256;
    world_recall_tick=world_recall_cooldown=world_heal_tick=world_hurt_tick=0;
    world_last_damage=-100;
    world_slot=0;material=hotbar[0];world_enemies_clear();world_spawn();changed=1;world_lighting_generation++;
    copy(message,"Camp ready / B craft / J journal",sizeof(message));
}
static int world_inventory_add(int kind,int count)
{
    if(kind<1||kind>=16||count<0||inventory[kind]>999-count)return 0;
    inventory[kind]+=count;return 1;
}
static int world_harvest(int index)
{
    int kind=world[index];
    if(world_mode!=1)return kind!=13 && kind!=16;
    if(kind==13||kind==16){copy(message,"Landmark / water protected",sizeof(message));return 0;}
    if((kind==9 && world_tool<1)||(kind==10 && world_tool<2)){
        copy(message,kind==9?"Need stone tools":"Need iron tools",sizeof(message));return 0;
    }
    if(!world_inventory_add(kind,1)){copy(message,"Inventory full",sizeof(message));return 0;}
    world_mined++;return 1;
}
static int world_place(int index)
{
    (void)index;
    if(material==13||material==14||material==16||material<1||material>16)return 0;
    if(world_mode==1){
        if(!inventory[material]){copy(message,"Collect or craft this material",sizeof(message));return 0;}
        inventory[material]--;
    }
    world_built++;return 1;
}
static int world_activate(int index)
{
    if(world_mode==0||world[index]!=13)return 0;
    int x=index%world_width,z=(index/world_width)%world_depth,y=index/(world_width*world_depth);
    if(ui_abs(x*256+128-px)+ui_abs(z*256+128-pz)+ui_abs(y*256+128-py)>6*256){
        copy(message,"Beacon too far",sizeof(message));return 1;
    }
    for(int i=0;i<4;i++)if(index==cell(site_x[i],site_y[i]+1,site_z[i])){
        if(world_sites&(1<<i)){copy(message,"Beacon is already restored",sizeof(message));return 1;}
        if(world_next_site()!=i){copy(message,"Follow the journal route",sizeof(message));return 1;}
        if(world_mode==1 && inventory[14]<site_cost[i]){copy(message,"Craft more cores / B",sizeof(message));return 1;}
        if(world_mode==1)inventory[14]-=site_cost[i];
        world_sites|=1<<i;world_health=20;changed=1;world_lighting_generation++;
        copy(message,world_sites==15?"Dawn restored / build freely":"Beacon restored / route unlocked",sizeof(message));return 1;
    }
    return 1;
}
static int world_craft(int recipe)
{
    if(recipe<0||recipe>=9)return 0;
    int next[17];for(int i=0;i<17;i++)next[i]=inventory[i];
    int a=0,b=0,c=0,na=0,nb=0,nc=0,out=0,amount=0,upgrade=0,heal=0;
    if(recipe==0){a=5;na=1;out=11;amount=4;}
    if(recipe==1){a=11;na=4;b=3;nb=6;upgrade=1;}
    if(recipe==2){a=3;na=4;b=9;nb=4;upgrade=2;}
    if(recipe==3){a=8;na=2;b=9;nb=1;c=10;nc=1;out=14;amount=1;}
    if(recipe==4){a=7;na=4;b=8;nb=1;out=4;amount=4;}
    if(recipe==5){a=11;na=1;b=8;nb=1;out=12;amount=4;}
    if(recipe==6){a=5;na=2;b=3;nb=4;out=15;amount=1;}
    if(recipe==7){a=6;na=2;heal=4;}
    if(recipe==8){a=2;na=4;b=7;nb=1;out=3;amount=4;}
    if(upgrade && (world_tool>=upgrade || (upgrade==2 && world_tool<1))){
        copy(message,"Check the tool upgrade order",sizeof(message));return 0;
    }
    if(heal && world_health==20){copy(message,"Health is already full",sizeof(message));return 0;}
    /* 拷贝检查、最后一次提交：材料不足/产物容量满/重复升级都不
     * 扣任何材料。只在全部前置条件满足后更新inventory与工具。 */
    if(world_mode==1){
        if(next[a]<na||next[b]<nb||next[c]<nc){copy(message,"Missing recipe materials",sizeof(message));return 0;}
        next[a]-=na;next[b]-=nb;next[c]-=nc;
    }
    if(out){if(next[out]>999-amount){copy(message,"Inventory full",sizeof(message));return 0;}next[out]+=amount;}
    for(int i=0;i<17;i++)inventory[i]=next[i];
    if(upgrade)world_tool=upgrade;if(heal)world_health=ui_clamp(world_health+heal,0,20);
    changed=1;copy(message,"Crafted / inventory updated",sizeof(message));return 1;
}
static void world_new_save(void)
{
    for(int i=0;i<WORLD2_HEADER;i++)save_bytes[i]=0;
    const char *magic="SWLD2MIO";for(int i=0;i<8;i++)save_bytes[i]=(u8)magic[i];
    store32(8,2);store32(12,world_width);store32(16,world_height);store32(20,world_depth);
    store32(24,world_cells);store32(32,world_mode);store32(36,world_seed);
    store32(40,px);store32(44,py);store32(48,pz);store32(52,yaw);store32(56,pitch);store32(60,material);
    for(int i=0;i<17;i++)store32(64+i*4,inventory[i]);
    store32(132,world_tool);store32(136,world_health);store32(140,world_sites);store32(144,world_elapsed);
    store32(148,world_deaths);store32(152,world_mined);store32(156,world_built);
    for(int i=0;i<4;i++)store32(160+i*4,site_y[i]);
    store32(176,world_stamina);store32(180,world_breath);
    for(int i=0;i<9;i++)store32(184+i*4,hotbar[i]);
    store32(220,world_slot);store32(224,world_kills);
    for(int i=0;i<world_cells;i++)save_bytes[WORLD2_HEADER+i]=world[i];
    store32(28,checksum(save_bytes+32,WORLD2_HEADER-32+world_cells));
    sc_mkdir("HOME/WORLD");int bytes=WORLD2_HEADER+world_cells;
    if(sc_write(world_save_path(),save_bytes,bytes)==bytes){changed=0;copy(message,"Expedition saved",sizeof(message));}
    else copy(message,"Save failed / world kept",sizeof(message));
}
static int world_new_load(void)
{
    u32 info[2];int bytes=WORLD2_HEADER+world_cells;
    if(sc_stat(world_save_path(),info)||info[0]!=1||info[1]!=(u32)bytes)return 0;
    if(sc_read(world_save_path(),save_bytes,bytes)!=bytes)return 0;
    const char *magic="SWLD2MIO";for(int i=0;i<8;i++)if(save_bytes[i]!=(u8)magic[i])return 0;
    if(load32(8)!=2||load32(12)!=(u32)world_width||load32(16)!=(u32)world_height
        ||load32(20)!=(u32)world_depth||load32(24)!=(u32)world_cells
        ||load32(28)!=checksum(save_bytes+32,WORLD2_HEADER-32+world_cells))return 0;
    int mode=(int)load32(32),x=(int)load32(40),y=(int)load32(44),z=(int)load32(48);
    int tool=(int)load32(132),health=(int)load32(136),sites=(int)load32(140);
    if(mode!=world_mode||x<256||x>=(world_width-1)*256||z<256||z>=(world_depth-1)*256
        ||y<400||y>=(world_height-1)*256||tool<0||tool>2||health<1||health>20||sites<0||sites>15)return 0;
    if(load32(60)==13||load32(60)==14)return 0;
    if(load32(52)>1023 || (int)load32(56)<-180 || (int)load32(56)>180 || load32(60)<1 || load32(60)>15)return 0;
    if(sites!=0&&sites!=1&&sites!=3&&sites!=7&&sites!=15)return 0;
    if(load32(36)>2000000000u || load32(36)==0)return 0;
    if(load32(144)>2000000000u||load32(148)>2000000000u||load32(152)>2000000000u||load32(156)>2000000000u)return 0;
    if(load32(176)>1000||load32(180)>1000||load32(220)>8||load32(224)>2000000000u)return 0;
    for(int i=0;i<9;i++)if(load32(184+i*4)<1||load32(184+i*4)>15||load32(184+i*4)==13||load32(184+i*4)==14)return 0;
    for(int i=228;i<WORLD2_HEADER;i++)if(save_bytes[i])return 0;
    for(int i=0;i<17;i++)if(load32(64+i*4)>999)return 0;
    for(int i=0;i<4;i++)if(load32(160+i*4)<2||load32(160+i*4)>18)return 0;
    for(int i=0;i<world_cells;i++)if(save_bytes[WORLD2_HEADER+i]>WORLD_KINDS)return 0;
    /* 读档事务在复制前检查不可变地基、四座信标和玩家身体。
     * 损坏的坐标即便仍落在世界范围内，也不能把人加载进实体块。 */
    for(int i=0;i<world_width*world_depth;i++)if(save_bytes[WORLD2_HEADER+i]!=3)return 0;
    for(int i=0;i<4;i++)if(save_bytes[WORLD2_HEADER+cell(site_x[i],(int)load32(160+i*4)+1,site_z[i])]!=13)return 0;
    for(int a=-1;a<=1;a+=2)for(int b=-1;b<=1;b+=2)for(int h=0;h<3;h++){
        int n=cell((x+a*48)>>8,(y-390+h*195)>>8,(z+b*48)>>8);
        if(n<0)return 0;int kind=save_bytes[WORLD2_HEADER+n];if(kind && kind!=16)return 0;
    }
    /* 此后才提交候选。先释放旧视角的捕获，清掉按住/拖动状态，
     * 再交换体素与玩家：旧世界的鼠标沿不能挖到刚读入的世界。
     * 放到全部校验之后，坏档拒绝不会打断仍有效的游戏输入。
     * 读档落在暂停菜单，玩家明确Resume才重新捕获并继续仿真。 */
    sc_mouse_capture(ui_win,0);world_captured=world_dragging=0;
    world_panel=4;world_page=0;ui_pressed=ui_buttons=0;
    for(int i=0;i<world_cells;i++)world[i]=save_bytes[WORLD2_HEADER+i];
    world_mode=mode;world_seed=(int)load32(36);px=x;py=y;pz=z;yaw=(int)load32(52);pitch=(int)load32(56);material=(int)load32(60);
    for(int i=0;i<17;i++)inventory[i]=(int)load32(64+i*4);
    world_tool=tool;world_health=health;world_sites=sites;world_elapsed=(int)load32(144);
    world_deaths=(int)load32(148);world_mined=(int)load32(152);world_built=(int)load32(156);
    for(int i=0;i<4;i++)site_y[i]=(int)load32(160+i*4);
    world_stamina=(int)load32(176);world_breath=(int)load32(180);world_slot=(int)load32(220);world_kills=(int)load32(224);
    for(int i=0;i<9;i++)hotbar[i]=(int)load32(184+i*4);
    world_recall_tick=world_recall_cooldown=0;world_heal_tick=world_hurt_tick=world_elapsed;
    world_last_damage=world_elapsed-100;
    world_sun_phase=(world_elapsed/5000)%12;
    int levels[12]={256,256,256,248,228,184,112,52,44,52,108,204};world_daylight=levels[world_sun_phase];
    world_enemies_clear();
    changed=0;jump_velocity=0;world_lighting_generation++;copy(message,"World restored",sizeof(message));return 1;
}
static void world_progress_tick(void)
{
    if(world_mode==0 || world_panel)return;
    if(world_elapsed<2000000000)world_elapsed+=4;
    int phase=(world_elapsed/5000)%12;
    int lights[12]={256,256,256,248,228,184,112,52,44,52,108,204};
    world_daylight=lights[phase]+(lights[(phase+1)%12]-lights[phase])*(world_elapsed%5000)/5000;
    if(phase!=world_sun_phase){world_sun_phase=phase;world_lighting_generation++;}
    int safe=world_safe(),night=phase>=7 && phase<=10;
    if(world_mode==1 && night && !safe && world_elapsed-world_hurt_tick>=1500){
        world_hurt_tick=world_last_damage=world_elapsed;world_health--;changed=1;
        copy(message,"Night exposure / find shelter",sizeof(message));
        if(!world_health){world_deaths++;world_health=20;world_spawn();copy(message,"Rescued at camp / items kept",sizeof(message));}
    }
    if(safe && world_health<20 && world_elapsed-world_heal_tick>=150){
        world_heal_tick=world_elapsed;world_health++;changed=1;
    }
    world_enemies_step();
    if(world_recall_tick){
        /* 受伤事实与伤害节流基点分开：新世界的节流0、读档的
         * 新基点都不是一次真实受伤，不能让刚进入营地的R误报
         * Recall interrupted。只有实际扣生命才写last_damage。 */
        if(ui_abs(px-world_recall_x)>64||ui_abs(pz-world_recall_z)>64||(!safe&&night)||world_elapsed-world_last_damage<100){
            world_recall_tick=0;copy(message,"Recall interrupted",sizeof(message));
        }else if(world_elapsed-world_recall_tick>=250){
            world_recall_tick=0;world_spawn();world_recall_cooldown=world_elapsed+3000;changed=1;
            copy(message,"Returned to camp",sizeof(message));
        }
    }
}
