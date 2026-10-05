/* ============================================================
 *  POC2 —— s3c 栈帧溢出演示 (scanf/gets 类)
 *  作者: mio   编译: 系统内 s3c poc2_overflow.c poc2_overflow.scx
 * ------------------------------------------------------------
 *  【原理】s3c 每个函数的标准序言:
 *      push ebp; mov ebp,esp; sub esp,N; push ebx; push esi; push edi
 *    即 [EBP+4] = 返回地址, [EBP] = 保存的 EBP, 局部变量在更低处。
 *  本程序的 vuln_read() 像 gets 一样把键盘输入**逐字节无边界**写进
 *  调用者的 char buf[24] —— 输入超过 24 字符就会继续向上覆盖:
 *    buf[24..] → 保存的 EBP → [EBP+4] 返回地址
 *  main 返回时 EIP = 你敲进去的 ASCII (比如 AAAA → 0x41414141)
 *  → ring3 异常 → 系统弹出 "APPLICATION PAUSED" 暂停卡片。
 *
 *  【修复】真实程序必须限量拷贝 (n < sizeof(buf)-1), 或用
 *  带边界的行读取封装 —— 缓冲区溢出是 C 时代最经典的漏洞类。
 * ============================================================ */
#include "SCAPI.H"

static int  win;
static int  echo_x;               /* 回显列游标 */
static char echo[2];              /* 单字符回显用的 NUL 结尾小串 */

static void put_str(int y, const char *s)
{
    sc_text(win, 6, y, s, PAL_TITLE);
}

/* ★ 漏洞函数: 像 gets 一样无边界拷贝。dst 指向调用者的栈缓冲。 */
static void vuln_read(char *dst)
{
    for (;;) {
        int k = sc_key();
        if (k == 10)              /* Enter (SCAPI 约定 Enter=10) 结束输入 */
            return;
        if (k == -1)              /* 队列空 */
            continue;
        if (k == 8) {             /* 退格: 回退一格并抹掉回显 */
            dst--;
            echo_x -= 8;
            sc_text(win, echo_x, 66, " ", PAL_CON_BG);
            continue;
        }
        if (k < 32 || k > 126)    /* 其余控制键忽略 */
            continue;
        *dst++ = (char)k;         /* ★ 无边界写入 —— 溢出发生在这里 */
        echo[0] = (char)k;
        echo[1] = 0;
        sc_text(win, echo_x, 66, echo, PAL_TITLE);
        echo_x += 8;
    }
}

int main(void)
{
    char buf[24];                 /* 24 字节栈缓冲 —— 溢出目标 */

    win = sc_open("POC2 overflow", 320, 120);
    sc_fill(win, 0, 0, 316, 104, PAL_CON_BG);
    put_str(6,  "POC2: scanf-style stack overflow (s3c EBP frame)");
    put_str(20, "type MORE than 24 chars, then Enter.");
    put_str(34, "the return address at [ebp+4] gets");
    put_str(46, "overwritten by your keystrokes.");
    put_str(66, "> ");
    echo_x = 22;                  /* "> " 是 2 字符 = 16px, 回显从 x=22 起 */

    vuln_read(buf);               /* ★ 溢出在这里发生 */

    /* 正常流程到不了下面 (返回地址已被输入字节覆盖):
     * main 的 leave/ret 会把 EIP 装成你敲的 ASCII → ring3 异常。 */
    put_str(80, "returned without crash?! bounds saved us.");
    sc_exit(0);
    return 0;
}
