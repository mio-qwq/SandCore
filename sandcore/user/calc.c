/* mio：M8原生计算器。32位有界算术供SCCC直接编译；不依赖long long。
 * 鼠标与键盘共用入口，保留31字节输入和原诊断。第一阶段尚未运行验证。 */
#include "SCAPI.H"
#include "NUI.inc"
static char input[64],answer[32];
static int n,answer_error;
static const char *keys[16]={"7","8","9","/","4","5","6","*","1","2","3","-","C","0","=","+"};
/* 幅值用无符号保存，负数才容许2147483648。先比较再乘十，避免
 * 超长输入绕回后通过检查；孤立符号不能被解析为零。 */
static int number(const char **p,int *out)
{
    while(**p==' ')(*p)++;
    int negative=0,digits=0;u32 value=0;
    if(**p=='-'||**p=='+'){negative=**p=='-';(*p)++;}
    u32 limit=negative?2147483648u:2147483647u;
    while(**p>='0'&&**p<='9'){
        u32 digit=(u32)(**p-'0');
        if(value>(limit-digit)/10u)return 0;
        value=value*10u+digit;(*p)++;digits++;
    }
    *out=(int)(negative?0u-value:value);return digits!=0;
}
static void error(const char *message)
{copy(answer,message,sizeof(answer));answer_error=1;}
static void evaluate(void)
{
    const char *p=input;int a,b;u32 bits;
    if(!number(&p,&a)){error("invalid number");return;}
    while(*p==' ')p++;
    char op=*p;if(*p)p++;
    if(!number(&p,&b)){error("invalid number");return;}
    while(*p==' ')p++;
    if(*p){error("unexpected text");return;}
    /* 无符号运算具有确定的模2^32语义，再按符号位判断范围；
     * 不执行有符号溢出，避免宿主优化器与SCCC产生不同解释。 */
    if(op=='+'){
        bits=(u32)a+(u32)b;
        if((~((u32)a^(u32)b)&((u32)a^bits))&0x80000000u){error("overflow");return;}
    }else if(op=='-'){
        bits=(u32)a-(u32)b;
        if((((u32)a^(u32)b)&((u32)a^bits))&0x80000000u){error("overflow");return;}
    }else if(op=='*'){
        int negative=(a<0)!=(b<0);
        u32 left=a<0?0u-(u32)a:(u32)a,right=b<0?0u-(u32)b:(u32)b;
        u32 limit=negative?2147483648u:2147483647u;
        if(right&&left>limit/right){error("overflow");return;}
        bits=left*right;if(negative)bits=0u-bits;
    }else if(op=='/'){
        if(!b){error("division by zero");return;}
        if(a==(int)0x80000000u&&b==-1){error("overflow");return;}
        bits=(u32)(a/b);
    }else{error("use + - * /");return;}
    answer_error=0;decimal(answer,(int)bits);
}
static void edit(int key)
{
    if(key=='c'||key=='C'){n=0;input[0]=0;copy(answer,"ready",sizeof(answer));answer_error=0;}
    else if(key==8&&n)input[--n]=0;
    else if(key==10)evaluate();
    else if(key>=32&&key<=126&&n<31){input[n++]=(char)key;input[n]=0;}
    ui_followup=1;
}
static void draw(void)
{
    ui_header("Calculator","Signed integers / exact 32-bit arithmetic");
    if(UI_W<260||UI_H<172){
        ui_text(16,36,"Enlarge to calculate",PAL_UI_MUTED);
        ui_small_control(98,16,60,112,"Enlarge",0);return;
    }
    int w=UI_W-32;if(w>520)w=520;
    int x=(UI_W-w)/2,top=ui_compact?32:74;
    int panel_h=ui_compact?32:70,keys_y=top+panel_h+(ui_compact?2:16);
    int gap=ui_compact?1:8,step=(UI_H-(ui_compact?22:30)-4-keys_y)/4;
    if(step>96)step=96;
    int cell=w/4,button=cell-gap,button_h=step-gap;
    ui_panel(x,top,w,panel_h);
    /* 裁剪只作用于视觉副本：窄窗口不会悄悄删掉表达式左半段。 */
    int columns=(w-16)/8,start=n>columns?n-columns:0;
    ui_clip_set(x+8,top,w-16,panel_h);
    ui_text(x+8,top+(ui_compact?0:8),input+start,PAL_UI_TEXT);
    ui_text(x+8,top+(ui_compact?16:40),answer,answer_error?PAL_UI_ALERT:PAL_UI_CYAN+7);
    ui_clip_clear();ui_small_control(17,UI_W-80,ui_compact?4:14,64,"Back",0);
    for(int i=0;i<16;i++){
        int bx=x+(i%4)*cell,by=keys_y+(i/4)*step;
        ui_button_box(bx,by,button,button_h,keys[i],i==14);
        if((ui_pressed&1)&&ui_hit(bx,by,button,button_h))ui_action=i+1;
    }
    ui_footer(ui_compact?"Enter =   C clear   Esc close":"Enter calculate / C clear / Backspace erase / Esc close");
}
int main(void)
{
    if(ui_open("Calculator / mio")<0)return 1;
    copy(answer,"ready",sizeof(answer));ui_followup=1;
    for(;;){
        if(!ui_frame_due())continue;
        ui_pointer();draw();ui_present();
        /* 动作帧不吞下一枚排队键，混合鼠标和键盘输入仍逐次生效。 */
        int action=ui_action,key=action?-1:sc_key();
        if(action==98){sc_window(ui_win,1);ui_followup=1;continue;}
        if(action>=1&&action<=16)key=action==15?10:keys[action-1][0];
        else if(action==17)key=8;
        if(key==27)return 0;
        if(key>=0)edit(key);
    }
}
