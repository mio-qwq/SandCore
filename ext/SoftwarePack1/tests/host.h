#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "font_rows.h"
static int checks,host_tick=100,host_fail_alloc,host_fail_read,host_fail_write;
static int host_key_input=-1,host_width=960,host_height=640,host_scale=100;
static int host_pointer_x,host_pointer_y,host_pointer_click;
static unsigned host_theme[32];
static int host_keys[256],host_audio_budget=100000,host_audio_frames,host_nonzero;
static short host_audio[400000];
typedef struct {char path[64];unsigned char *body;int size,kind;} HostFile;
static HostFile host_files[128];static int host_file_count;
static void check(int ok,const char *what,int line)
{checks++;if(!ok){fprintf(stderr,"FAIL line %d: %s\n",line,what);exit(1);}}
#define CHECK(x) check(!!(x),#x,__LINE__)
static HostFile *host_get(const char *path)
{for(int i=0;i<host_file_count;i++)if(!strcmp(path,host_files[i].path))return &host_files[i];return 0;}
static void host_put(const char *path,const void *data,int size)
{HostFile *f=host_get(path);if(!f){f=&host_files[host_file_count++];strncpy(f->path,path,63);}free(f->body);f->body=malloc(size+1);if(size)memcpy(f->body,data,size);f->body[size]=0;f->size=size;f->kind=1;}
intptr_t host_call(intptr_t n,intptr_t a,intptr_t b,intptr_t c,intptr_t d,intptr_t e,intptr_t f)
{
    (void)d;(void)e;(void)f;
    if(n==0x100){if(host_fail_alloc){host_fail_alloc--;return 0;}return (intptr_t)calloc(1,a);}
    if(n==0x101){free((void*)a);return 0;}
    if(n==2)return host_tick++;
    if(n==0x17)return host_keys[a&255];
    if(n==0x30){HostFile *file=host_get((char*)a);if(!file||host_fail_read)return -1;int count=file->size<c?file->size:c;memcpy((void*)b,file->body,count);return count;}
    if(n==0x31){if(host_fail_write)return -1;host_put((char*)a,(void*)b,c);return c;}
    if(n==0x37){HostFile *file=host_get((char*)a);if(!file)return -1;((unsigned*)b)[0]=file->kind;((unsigned*)b)[1]=file->size;return 0;}
    if(n==0x34){host_put((char*)a,"",0);host_get((char*)a)->kind=2;return 0;}
    if(n==0x35){HostFile *file=host_get((char*)a);if(!file)return -1;file->kind=0;return 0;}
    if(n==0x1B){int cp=(int)a;unsigned short *out=(void*)b;memset(out,0,32);if(cp>=32&&cp<=126)memcpy(out,host_font[cp-32],32);return cp<128?8:16;}
    if(n==0x230){unsigned *out=(void*)a;memset(out,0,64);out[0]=1;out[1]=1;out[2]=48000;return 0;}
    if(n==0x231)return 1;
    if(n==0x232){int count=c;if(count>host_audio_budget)count=host_audio_budget;if(count<0)count=0;host_audio_budget-=count;
        if(host_audio_frames+count<200000)memcpy(host_audio+host_audio_frames*2,(void*)b,count*4);
        const short *s=(void*)b;for(int i=0;i<count*2;i++)if(s[i])host_nonzero++;host_audio_frames+=count;return count;}
    if(n==0x233)return 0;
    if(n==0x14){int key=host_key_input;host_key_input=-1;return key;}
    if(n==0x77)return host_key_input;
    if(n==0x19){int *out=(void*)b;memset(out,0,20);out[2]=host_width;out[3]=host_height;out[4]=1;return 0;}
    if(n==0x20){unsigned *out=(void*)a;memset(out,0,32);out[0]=1920;out[1]=1080;out[2]=host_scale;return 0;}
    if(n==0x2C){memcpy((void*)a,host_theme,sizeof(host_theme));return 0;}
    if(n==0x22||n==0x78){int *out=(void*)b;memset(out,0,24);out[0]=host_pointer_x;out[1]=host_pointer_y;out[2]=out[3]=host_pointer_click;out[5]=1;if(n==0x22)host_pointer_click=0;return 0;}
    if(n==0x33){if(c)((char*)b)[0]=0;return 0;}
    if(n==0x81)return 0;
    return 0;
}
static void host_report(const char *name){printf("%s: %d checks passed (ASan + UBSan)\n",name,checks);}
/* NUI geometry and theme initialization, with exact-sized guarded buffers. */
#define HOST_UI() \
static void host_ui(int w,int h,int scale,int dark){ \
    if(ui_pixels)free(ui_pixels);host_width=ui_width=w;host_height=ui_height=h;host_scale=ui_scale=scale;UI_W=w*100/scale;UI_H=h*100/scale;ui_compact=UI_H<350;ui_focus=1;ui_pressed=ui_buttons=ui_action=0;host_pointer_x=host_pointer_y=host_pointer_click=0; \
    ui_pixels=calloc((size_t)w*h,4);ui_glyph_count=0; \
    for(int i=0;i<256;i++)ui_colors[i]=0x5F788C; \
    unsigned roles[24]={0xE6F3F9,0xD7E6ED,0xFBFCFE,0x173247,0x5F788C,0xD7E6ED,0x147F9F,0x173247,0x147F9F,0xFBFCFE,0x5F788C,0xED3548,0xEAC470,0x58BD73,0xA86BD4,0xED3548,0xFBFCFE,0x173247}; \
    if(dark){roles[0]=0x172536;roles[1]=0x18202A;roles[2]=0x0B1018;roles[3]=0xFBFCFE;roles[4]=0xAFA694;} \
    for(int i=0;i<24;i++)ui_theme[8+i]=roles[i]; \
    ui_theme[0]=ui_theme[2]=1;for(int i=0;i<32;i++)host_theme[i]=ui_theme[i]; \
    ui_colors[PAL_UI_PANEL]=roles[0];ui_colors[PAL_UI_INK]=roles[2];ui_colors[PAL_UI_TEXT]=roles[3];ui_colors[PAL_UI_MUTED]=roles[4];ui_colors[PAL_UI_LINE]=roles[5];ui_colors[PAL_UI_ALERT]=roles[11]; \
    for(int i=0;i<8;i++){ui_colors[PAL_UI_NIGHT+i]=roles[i>=6?8:1];ui_colors[PAL_UI_CYAN+i]=roles[6];ui_colors[PAL_UI_GOLD+i]=roles[12];} \
} \
static void host_image(const char *name){char path[160];snprintf(path,sizeof(path),"build/host/%s.ppm",name);FILE *f=fopen(path,"wb");CHECK(f!=0);fprintf(f,"P6\n%d %d\n255\n",ui_width,ui_height);for(int i=0;i<ui_width*ui_height;i++){unsigned p=ui_pixels[i];fputc((p>>16)&255,f);fputc((p>>8)&255,f);fputc(p&255,f);}fclose(f);}
