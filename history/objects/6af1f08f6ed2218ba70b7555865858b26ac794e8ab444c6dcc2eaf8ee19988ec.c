/* mio：体素着色器夹具，不是任务完成/游戏存档。
 * 为相同初始世界施加真实材料修改和光照条件，独立原生产物实际
 * 执行原世界函数；照片由每像素DDA/遮挡/反射/透射生成。 */
#define main world_program_main
#include "WNEW.inc"
#undef main
static volatile int light_probe[16];
static u32 light_blob[16+6*128*96+16];
int main(void)
{
    ui_win=sc_open_rgb("Voxel lighting / mio",408,226);
    if(ui_win<0)return 1;
    ui_pixels=(u32 *)sc_alloc(1920u*1080u*4u);if(!ui_pixels)return 2;
    ui_geometry();world_panel=0;generate();prepare_materials();
    u8 *magic=(u8 *)light_blob;char *signature="WPIX1MIO";
    for(int i=0;i<8;i++)magic[i]=(u8)signature[i];
    light_blob[2]=1;light_blob[3]=128;light_blob[4]=96;light_blob[5]=6;
    for(int i=0;i<ui_width*ui_height;i++)ui_pixels[i]=0xFF000000u|SCN_SHADE;
    for(int i=0;i<16;i++)light_blob[16+6*128*96+i]=0x534D494Fu;
    light_probe[0]=1;light_probe[2]=ui_win;
    ScnVec eye;scn_set(&eye,px,py,pz);
    for(int stage=0;stage<6;stage++){
        if(stage==1)world_daylight=48;
        if(stage==2){world_put(11,9,17,12);world_lighting_generation++;}
        if(stage==3){world_put(11,9,18,3);world_lighting_generation++;}
        if(stage==4){world_put(11,9,17,0);world_put(11,9,18,0);world_lighting_generation++;}
        if(stage==5){world_sites|=1;world_lighting_generation++;
            scn_set(&eye,site_x[0]*256+128,(site_y[0]+2)*256+128,(site_z[0]+3)*256+128);}
        int begin=sc_tick();u32 *pixels=light_blob+16+stage*128*96;
        for(int y=0;y<96;y++)for(int x=0;x<128;x++){
            ScnVec ray;scn_set(&ray,2*x+1-128,(96-2*y-1)/2-22,-96);
            pixels[y*128+x]=0xFF000000u|world_trace(&eye,&ray);
        }
        light_blob[8+stage]=(u32)(sc_tick()-begin);
        int left=(stage%3)*128,top=(stage/3)*96;
        for(int y=0;y<96;y++)for(int x=0;x<128;x++)
            ui_pixels[(top+y)*ui_width+left+x]=pixels[y*128+x];
        sc_frame32(ui_win,ui_pixels,(u32)(ui_width*ui_height*4));
        light_probe[1]=stage+1;sc_yield();
    }
    light_probe[3]=sc_write("HOME/WNEW.BIN",light_blob,(int)sizeof(light_blob));
    light_probe[4]=(int)ui_pixels;light_probe[5]=ui_width;light_probe[6]=ui_height;
    light_probe[0]=2;
    while(sc_key()!=27)sc_yield();sc_free(ui_pixels);return 0;
}
