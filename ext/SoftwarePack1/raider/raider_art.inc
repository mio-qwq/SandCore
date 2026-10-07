/* 清晰的装甲轮廓、关节、头盔、枪管与拾取物符号；材质来自包内
 * 命名色，不受极光/Classic 的控件文字映射影响。 */
static u32 rd_tex[RD_TEXN][RD_TS*RD_TS];
static u32 rd_spr[15][RD_SP*RD_SP];
static u32 rd_zbuf[1920];
static u32 rd_mix(u32 a,u32 b,int t)
{
    t=ex_clamp(t,0,256);
    u32 r=((a>>16&255)*(256-t)+(b>>16&255)*t)>>8;
    u32 g=((a>>8&255)*(256-t)+(b>>8&255)*t)>>8;
    u32 blue=((a&255)*(256-t)+(b&255)*t)>>8;
    return 0xFF000000u|(r<<16)|(g<<8)|blue;
}
static u32 rd_noise(u32 seed)
{u32 x=seed*2654435761u+0x9E3779B9u;x^=x>>13;x^=x<<17;x^=x>>5;return x&31;}
static void rd_gen_textures(void)
{
    for(int t=1;t<8;t++)for(int y=0;y<RD_TS;y++)for(int x=0;x<RD_TS;x++){
        u32 c=t==1?PK_METAL:t==2?PK_WOOD:t==3?PK_INK:t==4?PK_RED:t==5?PK_BLUE:t==6?PK_STONE:PK_GREEN;
        if(t<3){if(y%8==0||(x+(y/8%2)*8)%16==0)c=PK_SHADE;else c=rd_mix(c,PK_STONE,(int)rd_noise(y*RD_TS+x)*2);}
        else{
            if(x%16==0||y%16==0)c=PK_RUBBER;
            if(t==3&&(y==6||y==7))c=PK_CYAN;
            if(t>=4&&t<=6){if(x==3||x==28)c=PK_METAL;if(x>11&&x<21&&y>9&&y<23)c=PK_NIGHT;if(x==22&&y>14&&y<18)c=PK_WHITE;}
            if(t==7&&x>7&&x<25&&y>8&&y<24){c=PK_NIGHT;if(x==16||y==16)c=PK_GREEN;}
        }
        rd_tex[t][y*RD_TS+x]=0xFF000000u|(c&0xFFFFFFu);
    }
}
static void rd_art_rect(int id,int x,int y,int w,int h,u32 color)
{
    for(int r=ex_max(0,y);r<ex_min(RD_SP,y+h);r++)for(int c=ex_max(0,x);c<ex_min(RD_SP,x+w);c++)rd_spr[id][r*RD_SP+c]=0xFF000000u|color;
}
static void rd_gen_sprites(void)
{
    for(int s=0;s<15;s++)for(int i=0;i<RD_SP*RD_SP;i++)rd_spr[s][i]=0;
    for(int s=0;s<3;s++){
        u32 armor=s==0?PK_RED:s==1?PK_BLUE:PK_GREEN;
        rd_art_rect(s,11,2,11,9,PK_RUBBER);rd_art_rect(s,12,3,9,6,PK_METAL);
        rd_art_rect(s,12,6,9,2,s==2?PK_CYAN:PK_YELLOW);
        rd_art_rect(s,7,11,18,13,PK_RUBBER);rd_art_rect(s,8,12,16,10,armor);
        rd_art_rect(s,10,13,12,2,PK_WHITE);rd_art_rect(s,14,16,5,4,PK_METAL);
        rd_art_rect(s,4,11,6,9,PK_RUBBER);rd_art_rect(s,5,12,4,6,armor);
        rd_art_rect(s,23,11,6,10,PK_RUBBER);rd_art_rect(s,24,12,4,7,armor);
        rd_art_rect(s,9,23,6,8,PK_RUBBER);rd_art_rect(s,19,23,6,8,PK_RUBBER);
        rd_art_rect(s,10,24,4,4,PK_METAL);rd_art_rect(s,20,24,4,4,PK_METAL);
        if(s){rd_art_rect(s,18,17,14,4,PK_METAL);rd_art_rect(s,22,18,10,2,PK_NIGHT);rd_art_rect(s,19,21,4,3,PK_WOOD);}
        else{rd_art_rect(s,2,18,5,4,PK_WHITE);rd_art_rect(s,26,18,5,4,PK_WHITE);}
        if(s==2){rd_art_rect(s,3,10,8,5,PK_STONE);rd_art_rect(s,22,10,8,5,PK_STONE);}
    }
    rd_art_rect(3,4,27,24,4,PK_RUBBER);rd_art_rect(3,9,26,14,4,PK_METAL);
    /* 医疗箱、弹夹、霰弹、护甲有各自轮廓，避免只有一团纯色。 */
    rd_art_rect(4,7,18,18,12,PK_WHITE);rd_art_rect(4,12,16,8,2,PK_METAL);rd_art_rect(4,14,20,4,8,PK_RED);rd_art_rect(4,11,23,10,3,PK_RED);
    rd_art_rect(5,8,20,16,10,PK_WOOD);for(int x=10;x<24;x+=4)rd_art_rect(5,x,18,2,10,PK_GOLD);
    for(int x=9;x<24;x+=5){rd_art_rect(6,x,20,4,10,PK_RED);rd_art_rect(6,x,18,4,3,PK_GOLD);}
    rd_art_rect(7,9,15,14,15,PK_BLUE);rd_art_rect(7,6,14,7,6,PK_METAL);rd_art_rect(7,19,14,7,6,PK_METAL);rd_art_rect(7,12,19,8,7,PK_CYAN);
    for(int s=8;s<=9;s++){u32 c=s==8?PK_RED:PK_BLUE;rd_art_rect(s,8,19,7,7,c);rd_art_rect(s,10,21,3,3,PK_NIGHT);rd_art_rect(s,14,21,12,3,c);rd_art_rect(s,21,24,3,3,c);}
    rd_art_rect(10,8,21,16,9,PK_GOLD);rd_art_rect(10,10,19,12,3,PK_YELLOW);rd_art_rect(10,15,22,2,7,PK_WHITE);
    for(int s=11;s<=12;s++){rd_art_rect(s,4,22,25,4,PK_METAL);rd_art_rect(s,5,26,9,3,PK_WOOD);rd_art_rect(s,21,23,9,2,PK_NIGHT);if(s==12)rd_art_rect(s,16,26,5,4,PK_METAL);}
    /* 侦察无人机与最后一关的守卫机甲，外形与步兵不同。 */
    rd_art_rect(13,9,10,14,12,PK_METAL);rd_art_rect(13,12,13,8,6,PK_CYAN);
    for(int x=2;x<=24;x+=22){rd_art_rect(13,x,8,6,4,PK_RUBBER);rd_art_rect(13,x,22,6,4,PK_RUBBER);rd_art_rect(13,x+2,12,2,10,PK_METAL);}
    rd_art_rect(14,6,9,20,17,PK_RUBBER);rd_art_rect(14,7,10,18,13,PK_VIOLET);
    rd_art_rect(14,11,2,10,9,PK_METAL);rd_art_rect(14,12,5,8,3,PK_RED);
    rd_art_rect(14,1,10,8,9,PK_METAL);rd_art_rect(14,23,10,8,9,PK_METAL);
    rd_art_rect(14,7,26,7,6,PK_METAL);rd_art_rect(14,19,26,7,6,PK_METAL);
    rd_art_rect(14,18,17,14,5,PK_NIGHT);rd_art_rect(14,20,17,12,1,PK_GOLD);
}
