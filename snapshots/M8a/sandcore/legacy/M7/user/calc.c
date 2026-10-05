/* =====================================================================
 * mio：整数计算器，支持一条表达式的 + - * /，拒绝溢出与除零。
 * 当前输入限制为两个十进制有符号数与一个操作符，不声称完整表达式语言。
 * 用 i64 做中间结果但不做 i64 除法，避免引入 libgcc；最后检查 i32 范围。
 * -2147483648 / -1 单独拒绝，防止 x86 idiv 触发全系统异常红屏。
 * ===================================================================== */
#include "api.h"
static char input[64],answer[32];
static int win,n;
/* 解析会推进调用者的游标，让 evaluate 能接着读操作符。使用 u32 幅值
 * 而非 i32 累加：负数允许幅值 2147483648，正数只到 2147483647。
 * 检查 value <= (limit-digit)/10 后再乘十，避免先溢出再比较已绕回的值。
 * 输入负号但没有数字必须失败，不能把孤立的 '-' 当作合法零。 */
static int number(const char **p,int *out)
{
    while(**p==' ') (*p)++;
    int negative=0,digits=0; u32 value=0;
    if(**p=='-' || **p=='+') { negative=**p=='-'; (*p)++; }
    u32 limit=negative?2147483648u:2147483647u;
    while(**p>='0'&&**p<='9') {
        u32 d=(u32)(**p-'0');
        if(value>(limit-d)/10) return 0;
        value=value*10+d; (*p)++; digits++;
    }
    *out=(int)(negative?0u-value:value);
    return digits!=0;
}
/* 语法严格为“有符号十进制数 运算符 有符号十进制数”，空格可忽略，
 * 尾部额外字符一律拒绝；所以 1+2*3 不会被误报为算出了整个表达式。
 * 加/减/乘先把左操作数提升到 i64，防止 i32 运算先溢出；结果再检查
 * i32 范围。除法用 i32，先挡住唯一两个 idiv 异常条件，从而无需
 * libgcc 的 64 位除法辅助例程，也不会因用户表达式触发内核 panic。 */
static void evaluate(void)
{
    const char *p=input; int a,b;
    if(!number(&p,&a)) { copy(answer,"invalid number",32); return; }
    while(*p==' ') p++;
    char op=*p; if(*p) p++;
    if(!number(&p,&b)) { copy(answer,"invalid number",32); return; }
    while(*p==' ') p++;
    if(*p) { copy(answer,"unexpected text",32); return; }
    long long value;
    if(op=='+') value=(long long)a+b;
    else if(op=='-') value=(long long)a-b;
    else if(op=='*') value=(long long)a*b;
    else if(op=='/') {
        if(!b) { copy(answer,"division by zero",32); return; }
        if(a==(int)0x80000000u && b==-1) { copy(answer,"overflow",32); return; }
        value=a/b;
    } else { copy(answer,"use + - * /",32); return; }
    if(value>2147483647LL || value<-2147483647LL-1) copy(answer,"overflow",32);
    else decimal(answer,(int)value);
}
static void draw(void)
{
    page(win,"CALCULATOR / integer","ENTER calculate  C clear  ESC close");
    sc_text(win,8,42,"expression",PAL_WIN_TITLE);
    sc_text(win,8,60,input,PAL_TITLE);
    sc_fill(win,8,84,266,28,PAL_TASKBAR);
    sc_text(win,16,94,answer,PAL_CON_TINT);
}
void main(void)
{
    win=sc_open("Calculator",290,164); if(win<0) return;
    copy(answer,"ready",32); draw();
    for(;;) {
        int key=sc_key(); if(key<0) continue;
        if(key==27) return;
        /* 只显示一行表达式，输入长度限 31；缓冲更大是为终止符和后续
         * 扩展留余量，不表示画布能显示全部 63 字符。每次编辑后统一重绘
         * 消除退格残字；Enter 才求值，输入中的不完整表达式不会弹错误。 */
        if(key=='c'||key=='C') { n=0; input[0]=0; copy(answer,"ready",32); }
        else if(key==8 && n) input[--n]=0;
        else if(key==10) evaluate();
        else if(key>=32&&key<=126&&n<31) { input[n++]=(char)key; input[n]=0; }
        draw();
    }
}
