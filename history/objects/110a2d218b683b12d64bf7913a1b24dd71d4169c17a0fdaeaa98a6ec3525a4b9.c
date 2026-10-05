/* mio：只读场景输入，真实三环渲染；不灌宿主预算像素。 */
#define main race_original_main
#include "RACE.inc"
#undef main
#undef scn_reset
#undef scn_prepare_exact
static volatile int race_probe[16];
int main(void){
    if(ui_open("RACE full pixels / mio")<0)return 1;
    int width=ui_width,height=ui_height,body=width*height*4*3,bytes=64+body+64;
    u32 *output=sc_alloc((u32)bytes);if(!output)return 2;
    for(int i=0;i<16;i++){output[i]=0;output[(64+body)/4+i]=0x534D494Fu;}
    textures();ui_x=ui_y=-100;ui_buttons=0;ui_focus=1;
    for(int scene=0;scene<3;scene++){
        race_track=scene;race_legacy=0;race_mode=2;race_menu=race_finished=0;
        race_restart();distance=12000+scene*11250;steer=scene==0?-40:scene==1?70:15;
        speed=48;lap=0;lap_start=0;race_elapsed=750;race_place=3;race_countdown=0;
        race_boost=750;race_condition=920;race_drift=race_boosting=race_pit=0;
        for(int i=0;i<RACE_RIVALS;i++)rival_distance[i]=distance+400+i*460;
        int began=sc_tick();draw();int elapsed=sc_tick()-began;
        if(scn_active->error)fixture_errors++;
        output[8+scene]=(u32)elapsed;race_probe[4+scene]=elapsed;
        for(int i=0;i<width*height;i++)output[16+scene*width*height+i]=ui_pixels[i];
        ui_present();
    }
    char *magic="RFRM1MIO";for(int i=0;i<8;i++)((u8 *)output)[i]=(u8)magic[i];
    output[2]=1;output[3]=(u32)width;output[4]=(u32)height;output[5]=3;
    output[6]=(u32)fixture_errors;output[7]=(u32)body;
    race_probe[1]=sc_write("HOME/RFRAME-OLD.BIN",output,bytes);race_probe[2]=width;race_probe[3]=height;
    race_probe[7]=fixture_errors;race_probe[8]=(int)ui_pixels;
    race_probe[9]=sc_write("HOME/RNOTE-OLD.BIN",fixture_notes,fixture_note_count*64);
    race_probe[0]=2;while(sc_key()!=27)sc_yield();sc_free(output);return fixture_errors;
}
