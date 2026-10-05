/* mio：普通Shell的独立任务，原目标结束后真实复用PID。
 * 原调试器不能把它的窗口/寄存器/内存当已结束的拥有目标。 */
#include "SCAPI.H"
int main(void){int w=sc_open("Independent reused task / mio",260,100);if(w<0)return 1;sc_text(w,8,12,"Shell owned / mio",PAL_UI_TEXT);wait_escape();return 7;}
