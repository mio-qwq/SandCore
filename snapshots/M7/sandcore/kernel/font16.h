#ifndef SANDCORE_FONT16_H
#define SANDCORE_FONT16_H

/* =====================================================================
 *  SandCore 中文字体 SCF-16 —— 接口层
 *  ---------------------------------------------------------------------
 *  【字形数据不在这里!】
 *    真正的字形表是 kernel/font16.txt（用户 mio 所有，统一凤凰点阵体）；
 *    代理补字只能使用 tools/import_font.py，不直接手改用户字形。
 *    make 时由 tools/mkfont.py 编译成 build/font16_data.h,
 *    本文件负责把数据接进来并提供查找函数。
 *
 *  【给未来的自己/字库导入】
 *    渲染器 (gfx_char16) 只认 zh16_t 这个 16 行×16 列的数据结构 ——
 *    以后从开源点阵字库批量导出字表, 只要生成同格式的表就能直接用,
 *    渲染器零改动。这就是"字体接口"。
 * ===================================================================== */

#include "font16_data.h"      /* 生成文件: zh16_t / zh16_tab[] / ZH16_N */

/* ---- 运行时字体切换 (M5) ----
 * 内核启动时从 SandFS 读 sys/font.scf (SCF1MIO 格式) 调 font16_use():
 * 文件字体成为默认字体; 读不到就回落到上面编译的内置表。 */
static const zh16_t *zh16_active;      /* 0 = 用编译的内置表 */
static int zh16_active_n;

static void font16_use(const zh16_t *tab, int n)
{
    if (tab && n > 0) {
        zh16_active = tab;
        zh16_active_n = n;
    }
}

/* 按 UTF-8 3 字节精确匹配查字形; 磁盘激活后仅查磁盘表，否则查同源内置表；未收字返回 0
 * (gfx 层会画空心框) */
static const zh16_t *zh16_find(const char *utf8)
{
    const zh16_t *tab = zh16_active ? zh16_active : zh16_tab;
    int n = zh16_active ? zh16_active_n : ZH16_N;
    for (int i = 0; i < n; i++)
        if (tab[i].zh[0] == utf8[0]
         && tab[i].zh[1] == utf8[1]
         && tab[i].zh[2] == utf8[2])
            return &tab[i];
    return 0;
}

#endif /* SANDCORE_FONT16_H */
