/* =====================================================================
 * mio：夜影实体与玩家碰撞。最多六只，逻辑位置为Q8世界坐标。
 * 它们沿真实地面移动，实体块阻挡攻击，营地/修复遗迹/照明排斥。
 * 击败后先检查晶体容量再提交掉落，库存满不吞掉奖励。夜影不写
 * 入永久存档：读取/新世界先清理，日夜状态决定后续自然生成。
 * 绘制用真实球体求交，并与体素首命中比较深度；绝不能隔墙显示。
 * ===================================================================== */
static int shade_x[6],shade_y[6],shade_z[6],shade_hp[6],shade_last_spawn,shade_attack_tick;
static int world_body_clear(int x,int y,int z)
{
    if(x<256||z<256||x>=(world_width-1)*256||z>=(world_depth-1)*256||y<400||y>=(world_height-1)*256)return 0;
    for(int a=-1;a<=1;a+=2)for(int b=-1;b<=1;b+=2)for(int h=0;h<3;h++)
        if(block(x+a*48,y-390+h*195,z+b*48))return 0;
    return 1;
}
static int world_line_clear(int x,int y,int z,int xx,int yy,int zz)
{
    int length=ui_abs(xx-x)+ui_abs(yy-y)+ui_abs(zz-z),steps=length/64+1;
    if(steps>96)steps=96;
    for(int i=1;i<steps;i++)if(block(x+(xx-x)*i/steps,y+(yy-y)*i/steps,z+(zz-z)*i/steps))return 0;
    return 1;
}
static int world_position_safe(int x,int y,int z)
{
    if(ui_abs(x-12*256-128)+ui_abs(z-18*256-128)<9*256)return 1;
    for(int i=0;i<4;i++)if((world_sites&(1<<i)) && ui_abs(x-site_x[i]*256-128)+ui_abs(z-site_z[i]*256-128)<8*256)return 1;
    for(int zz=(z>>8)-3;zz<=(z>>8)+3;zz++)for(int xx=(x>>8)-3;xx<=(x>>8)+3;xx++)
        for(int yy=(y>>8)-3;yy<=(y>>8)+1;yy++){
            int n=cell(xx,yy,zz);if(n>=0&&(world[n]==15||world[n]==12))return 1;
        }
    return 0;
}
static void world_enemies_clear(void)
{for(int i=0;i<6;i++)shade_hp[i]=0;shade_last_spawn=world_elapsed;shade_attack_tick=0;}
static void world_enemies_step(void)
{
    if(world_mode!=1)return;
    int night=world_sun_phase>=7&&world_sun_phase<=10;
    if(!night){for(int i=0;i<6;i++)shade_hp[i]=0;return;}
    if(!world_safe() && world_elapsed-shade_last_spawn>=500){
        shade_last_spawn=world_elapsed;
        for(int i=0;i<6;i++)if(!shade_hp[i]){
            int angle=(world_elapsed*7+i*177)&1023;
            int x=ui_clamp(px+scn_sin(angle)*8,2*256,(world_width-3)*256);
            int z=ui_clamp(pz+scn_cos(angle)*8,2*256,(world_depth-3)*256);
            int y=(world_top(x>>8,z>>8)+1)*256+136;
            if(y<(world_height-2)*256 && !world_position_safe(x,y,z)){
                shade_x[i]=x;shade_y[i]=y;shade_z[i]=z;shade_hp[i]=6;
            }
            break;
        }
    }
    for(int i=0;i<6;i++)if(shade_hp[i]){
        int x=shade_x[i],z=shade_z[i],dx=ui_clamp(px-x,-8,8),dz=ui_clamp(pz-z,-8,8);
        if(world_position_safe(x,shade_y[i],z)){shade_hp[i]=0;continue;}
        int xx=x+dx,zz=z+dz,y=(world_top(xx>>8,zz>>8)+1)*256+136;
        if(ui_abs(y-shade_y[i])<=256 && !block(xx,y,zz) && !block(xx,y-100,zz)){
            shade_x[i]=xx;shade_z[i]=zz;shade_y[i]=y;
        }
        if(ui_abs(shade_x[i]-px)+ui_abs(shade_z[i]-pz)<300 && ui_abs(shade_y[i]-py)<512
            && world_elapsed-world_hurt_tick>=100 && !world_safe()
            && world_line_clear(px,py,pz,shade_x[i],shade_y[i],shade_z[i])){
            world_hurt_tick=world_last_damage=world_elapsed;world_recall_tick=0;world_health-=2;changed=1;
            copy(message,"Night shade / fight or shelter",sizeof(message));
            if(world_health<=0){world_deaths++;world_health=20;world_spawn();world_enemies_clear();return;}
        }
    }
}
static int world_enemy_attack(void)
{
    if(world_mode!=1)return 0;
    int fx=scn_sin(yaw),fz=scn_cos(yaw),chosen=-1,nearest=1024;
    for(int i=0;i<6;i++)if(shade_hp[i]){
        int dx=shade_x[i]-px,dz=shade_z[i]-pz,forward=(dx*fx+dz*fz)/256,side=(dx*fz-dz*fx)/256;
        if(forward>0&&forward<nearest&&ui_abs(side)<150&&ui_abs(shade_y[i]-py-pitch*forward/256)<180
            && world_line_clear(px,py,pz,shade_x[i],shade_y[i],shade_z[i])){chosen=i;nearest=forward;}
    }
    if(chosen<0)return 0;
    if(world_elapsed-shade_attack_tick<30)return 1;
    shade_attack_tick=world_elapsed;
    int damage=2+world_tool;
    if(shade_hp[chosen]<=damage){
        if(!world_inventory_add(10,1)){copy(message,"Crystal reward / inventory full",sizeof(message));return 1;}
        shade_hp[chosen]=0;world_kills++;changed=1;copy(message,"Shade defeated / crystal +1",sizeof(message));
    }else{shade_hp[chosen]-=damage;copy(message,"Shade hit",sizeof(message));}
    return 1;
}
static void world_enemies_draw(void)
{
    int h=world_view_bottom-world_view_top,fx=scn_sin(yaw),fz=scn_cos(yaw),drawn=0;
    ScnVec eye;scn_set(&eye,px,py,pz);
    for(int pass=0;pass<6;pass++){
        int chosen=-1,far=0;
        for(int i=0;i<6;i++)if(shade_hp[i]&&!(drawn&(1<<i))){
            int forward=((shade_x[i]-px)*fx+(shade_z[i]-pz)*fz)/256;
            if(forward>200&&forward<8000&&forward>far&&ui_abs(shade_x[i]-px)<7800&&ui_abs(shade_z[i]-pz)<7800){chosen=i;far=forward;}
        }
        if(chosen<0)break;drawn|=1<<chosen;
        int dx=shade_x[chosen]-px,dz=shade_z[chosen]-pz,side=(dx*fz-dz*fx)/256;
        int cx=ui_width/2+side*world_focal/far;
        int cy=world_view_top+h/2-(shade_y[chosen]-py)*world_focal/far+pitch*world_focal/256;
        int radius=ui_clamp(160*world_focal/far,1,ui_height);
        int left=ui_clamp(cx-radius,0,ui_width),right=ui_clamp(cx+radius,0,ui_width);
        int top=ui_clamp(cy-radius,world_view_top,world_view_bottom),bottom=ui_clamp(cy+radius,world_view_top,world_view_bottom);
        scn_reset();scn_floor=0;
        scn_sphere(dx,shade_y[chosen]-py,dz,138,SCN_SHADE,40,0);
        scn_sphere(dx-fx*100/256+fz*44/256,shade_y[chosen]-py+24,dz-fz*100/256-fx*44/256,19,SCN_SUN,0,0);
        scn_sphere(dx-fx*100/256-fz*44/256,shade_y[chosen]-py+24,dz-fz*100/256+fx*44/256,19,SCN_SUN,0,0);
        for(int yy=top;yy<bottom;yy++)for(int xx=left;xx<right;xx++){
            int sideways=2*xx+1-ui_width,vertical=(h-2*(yy-world_view_top)-1)/2+pitch*world_focal/256;
            ScnVec ray;scn_set(&ray,(fx*world_focal*2+fz*sideways)/512,vertical,(fz*world_focal*2-fx*sideways)/512);
            /* 以玩家为局部原点，避免64格世界超过通用SCENE的8192
             * 几何范围；真实体素仍用原世界坐标求交作深度遮挡。 */
            ScnVec origin;scn_set(&origin,0,0,0);ScnHit entity;
            scn_intersect(&origin,&ray,&entity,scn_ratio16(8000,scn_length(&ray)));
            if(!entity.found)continue;
            WorldHit wall;world_cast(&eye,&ray,8000,-1,&wall);
            if(wall.found && wall.distance<scn_displace(scn_length(&ray),entity.t))continue;
            u32 color=scn_trace(&origin,&ray);
            ui_pixels[yy*ui_width+xx]=0xFF000000u|scn_mix(color,SCN_GLASS,45);
        }
    }
}
