/* mio：共用路径框的普通三环行为探针。路径含已收录凤凰字形；
 * 验证器只从真实页表读取path_probe_text，输入必须经HMP/QMP，
 * 不以改客体内存伪造中文输入。退出仍走普通任务/画布回收。 */
#include "SCAPI.H"
#include "NUI.inc"
static char path_probe_text[64]="HOME/沙核";
static volatile int path_probe_stage,path_probe_result;
int main(void)
{
    if(ui_open("Path probe")<0)return 1;
    path_probe_stage=1;
    path_probe_result=ui_edit_path(path_probe_text,64,"UTF-8 / Right then Backspace");
    path_probe_stage=2;
    for(;;){
        if(!ui_frame_due())continue;
        ui_pointer();ui_header("PATH / PHOENIX",path_probe_text);
        ui_footer("Esc closes / private UTF-8 test");ui_present();
        if(sc_key()==27)return 0;
    }
}
