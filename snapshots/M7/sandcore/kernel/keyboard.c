/* =====================================================================
 *  SandCore PS/2 键盘驱动 (kernel/keyboard.c)
 *  ---------------------------------------------------------------------
 *  键盘走 8042 控制器: 状态口 0x64, 数据口 0x60, 中断 IRQ1。
 *  QEMU 发的是 XT 扫描码集 1: 按下 = 通码, 松开 = 断码(通码|0x80)。
 *  本驱动的职责: 通码 -> ASCII (含 Shift/CapsLock), 环形缓冲排队,
 *  上层 getkey() 阻塞取键。
 *
 *  映射表是我们自己的布局定义 (US 布局语义, 编码自制),
 *  与 docs/INTR.md 的扫描码表一一对应。
 * ===================================================================== */
#include "io.h"
#include "keyboard.h"

extern void wm_key_input(char c);   /* wm.c: 送进聚焦窗口的键队列 (M6) */

#define KBD_DATA 0x60                 /* 数据口: 读扫描码 */
#define KBD_STAT 0x64                 /* 状态口: bit0=输出缓冲有数据 */

/* ---------------- 环形缓冲 ---------------- */
#define QSIZE 32
static volatile char kq[QSIZE];
static volatile u8   qh, qt;          /* 队头/队尾 */

static void kpush(char c)
{
    u8 n = (u8)((qh + 1) % QSIZE);
    if (n != qt) {                    /* 满了就丢新键 (人手速丢不起) */
        kq[qh] = c;
        qh = n;
    }
}

int kbhit(void)      { return qt != qh; }

int key_pop(void)    /* 非阻塞弹出一个键 (系统调用 SYS_GETKEY 用) */
{
    if (qt == qh)
        return -1;
    char c = kq[qt];
    qt = (u8)((qt + 1) % QSIZE);
    return c;
}

int getkey(void)
{
    for (;;) {
        if (qt != qh) {
            char c = kq[qt];
            qt = (u8)((qt + 1) % QSIZE);
            return c;
        }
        halt();                       /* 睡到任意中断 (时钟/键盘) 再查 */
    }
}

/* ---------------- 扫描码 -> ASCII ----------------
 * 下标 = 通码。两个表: 平常 / Shift 按住。0 = 该键不产生字符。 */
static const char map_base[0x80] = {
    [0x01] = 27,                                     /* Esc */
    [0x02] = '1',  [0x03] = '2',  [0x04] = '3',
    [0x05] = '4',  [0x06] = '5',  [0x07] = '6',
    [0x08] = '7',  [0x09] = '8',  [0x0A] = '9',
    [0x0B] = '0',  [0x0C] = '-',  [0x0D] = '=',
    [0x0E] = '\b',                                   /* Backspace */
    [0x0F] = '\t',
    [0x10] = 'q',  [0x11] = 'w',  [0x12] = 'e',
    [0x13] = 'r',  [0x14] = 't',  [0x15] = 'y',
    [0x16] = 'u',  [0x17] = 'i',  [0x18] = 'o',
    [0x19] = 'p',  [0x1A] = '[',  [0x1B] = ']',
    [0x1C] = '\n',                                   /* Enter */
    [0x1E] = 'a',  [0x1F] = 's',  [0x20] = 'd',
    [0x21] = 'f',  [0x22] = 'g',  [0x23] = 'h',
    [0x24] = 'j',  [0x25] = 'k',  [0x26] = 'l',
    [0x27] = ';',  [0x28] = '\'', [0x29] = '`',
    [0x2B] = '\\',
    [0x2C] = 'z',  [0x2D] = 'x',  [0x2E] = 'c',
    [0x2F] = 'v',  [0x30] = 'b',  [0x31] = 'n',
    [0x32] = 'm',  [0x33] = ',',  [0x34] = '.',
    [0x35] = '/',  [0x37] = '*',  [0x39] = ' ',
};

static const char map_shift[0x80] = {
    [0x01] = 27,
    [0x02] = '!',  [0x03] = '@',  [0x04] = '#',
    [0x05] = '$',  [0x06] = '%',  [0x07] = '^',
    [0x08] = '&',  [0x09] = '*',  [0x0A] = '(',
    [0x0B] = ')',  [0x0C] = '_',  [0x0D] = '+',
    [0x0E] = '\b',
    [0x0F] = '\t',
    [0x10] = 'Q',  [0x11] = 'W',  [0x12] = 'E',
    [0x13] = 'R',  [0x14] = 'T',  [0x15] = 'Y',
    [0x16] = 'U',  [0x17] = 'I',  [0x18] = 'O',
    [0x19] = 'P',  [0x1A] = '{',  [0x1B] = '}',
    [0x1C] = '\n',
    [0x1E] = 'A',  [0x1F] = 'S',  [0x20] = 'D',
    [0x21] = 'F',  [0x22] = 'G',  [0x23] = 'H',
    [0x24] = 'J',  [0x25] = 'K',  [0x26] = 'L',
    [0x27] = ':',  [0x28] = '"',  [0x29] = '~',
    [0x2B] = '|',
    [0x2C] = 'Z',  [0x2D] = 'X',  [0x2E] = 'C',
    [0x2F] = 'V',  [0x30] = 'B',  [0x31] = 'N',
    [0x32] = 'M',  [0x33] = '<',  [0x34] = '>',
    [0x35] = '?',
};

/* ---------------- 修饰键状态 ---------------- */
static u8 shift_l, shift_r, caps;
static u8 extended;
static u8 held[256];
int keyboard_down(u32 key) { return key<256 ? held[key] : 0; }

/* ---------------- IRQ1 处理 ---------------- */
void keyboard_isr(void)
{
    if (!(inb(KBD_STAT) & 0x01))
        return;                       /* 不是我们的数据 (防御性) */

    u8 sc = inb(KBD_DATA);

    if(sc==0xE0) { extended=1; return; }
    if(sc==0xE1) return;
    if(extended) {
        /* E0 后的一字节属于扩展扫描码，不能再落入普通 ASCII 表。
         * 方向键使用 0x80..0x83 的沙核事件码；松开只清状态不送事件。
         * F2/F3/F5 用低位控制码，便于无 Ctrl 状态的 PS/2 编辑器保存/重载。 */
        extended=0;
        u8 key=0,code=sc&0x7F;
        if(code==0x48) key=0x80;
        if(code==0x50) key=0x81;
        if(code==0x4B) key=0x82;
        if(code==0x4D) key=0x83;
        if(key) { held[key]=!(sc&0x80); if(!(sc&0x80)) wm_key_input((char)key); }
        return;
    }
    if(sc>=0x3B && sc<=0x44) {
        /* F8/F9/F10 不能继续借用 8/9/10：这些值已经分别属于
         * Backspace/Tab/Enter。M6 的 F2/3/5 数值保留；高功能键
         * 使用独立事件 0x88..0x8A，IDE 才能同时编辑和单步。 */
        int event=sc-0x3A;
        if(event>=8) event+=0x80;
        wm_key_input((char)event);
        return;
    }

    if (sc == 0x2A) { shift_l = 1; return; }   /* 左Shift 按下 */
    if (sc == 0x36) { shift_r = 1; return; }
    if (sc == 0xAA) { shift_l = 0; return; }   /* 松开 (通码|0x80) */
    if (sc == 0xB6) { shift_r = 0; return; }
    if (sc == 0x3A) { caps ^= 1;   return; }   /* CapsLock 翻转 */
    /* 方向/油门读取按住状态，不能依赖文字重复速率。字母物理键统一
     * 按小写索引，Shift/Caps 只改变排队文字；断码必须先清 held。 */
    if(map_base[sc&0x7F]) held[(u8)map_base[sc&0x7F]]=!(sc&0x80);
    if (sc & 0x80)  return;

    u8 sh = shift_l | shift_r;
    char c = sh ? map_shift[sc] : map_base[sc];
    if (!c)
        return;

    /* 字母键: Shift 与 CapsLock 的效果取异或 */
    if (c >= 'a' && c <= 'z' && (sh ^ caps))
        c = (char)(c - 'a' + 'A');
    else if (c >= 'A' && c <= 'Z' && !(sh ^ caps))
        c = (char)(c - 'A' + 'a');

    kpush(c);
    wm_key_input(c);              /* 同步喂给窗口系统的聚焦队列 */
}
