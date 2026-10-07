#include "host.h"
#define main installer_entry
#include "../installer/installer.c"
#undef main
HOST_UI()
int main(void)
{
    for(int i=0;i<EXAPP_COUNT;i++){
        const exapp_entry *a=&EXAPP[i];unsigned char *raw=malloc(a->scx_size);CHECK(!ins_unpack(a->scx,a->scx_packed_size,raw,a->scx_size));CHECK(ins_crc32(raw,a->scx_size)==a->scx_crc);CHECK(ins_unpack(a->scx,a->scx_packed_size-1,raw,a->scx_size)<0);CHECK(ins_unpack(a->scx,a->scx_packed_size,raw,a->scx_size-1)<0);free(raw);
    }
    unsigned char invalid[]={ 'S','P','L','Z','1','M','I','O',3,0,0,0,0,0,0},dest[3];CHECK(ins_unpack(invalid,sizeof(invalid),dest,3)<0);
    ins_load_icons();for(int i=0;i<EXAPP_COUNT;i++)CHECK(ins_icons[i]!=0);
    CHECK(ins_valid_dir("HOME/APPS"));CHECK(!ins_valid_dir("HOME/../SYS"));CHECK(!ins_valid_dir("C:\\APPS"));
    host_put("SYS/MENU.CFG","Other|/APPS/OTHER.SCX|/SYS/ICONS/OTHER.SCB\n",42);ins_menu=1;ins_install_app(0);CHECK(ins_done==1&&!ins_fail);CHECK(host_get("HOME/APPS/PCALC.SCX"));CHECK(host_get("HOME/APPS/ICONS/PCALC.SCB"));CHECK(host_get("DESK/PCALC.LNK"));
    ins_menu_entry(0,1);HostFile *menu=host_get("SYS/MENU.CFG");char *first=strstr((char*)menu->body,"HOME/APPS/PCALC.SCX");CHECK(first&&strstr(first+1,"HOME/APPS/PCALC.SCX")==0);
    HostFile *cfg=host_get("HOME/PCALX.CFG");CHECK(cfg!=0);host_put(cfg->path,"keypad=0\n",9);ins_install_app(0);CHECK(!strcmp((char*)host_get("HOME/PCALX.CFG")->body,"keypad=0\n"));
    host_fail_write=1;int failures=ins_fail;ins_install_app(1);CHECK(ins_fail==failures+1);host_fail_write=0;
    host_fail_read=1;CHECK(ins_menu_entry(0,1)<0);host_fail_read=0;
    host_ui(960,640,100,0);ins_draw_comps();host_image("installer-aurora");host_ui(641,481,200,0);ins_draw_comps();host_image("installer-200");
    ins_page=PG_OPTIONS;int count=host_file_count;host_key_input=27;ins_action(PK_ABOUT_ID);CHECK(ins_page==PG_OPTIONS&&host_file_count==count);host_image("about-installer-200");host_report("installer/payload");return 0;
}
