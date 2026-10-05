#ifndef SANDCORE_PALETTE_H
#define SANDCORE_PALETTE_H

/* =====================================================================
 *  SandCore 调色板槽位分配 —— 唯一权威定义
 *  ---------------------------------------------------------------------
 *  与 docs/GFX.md 第 2 节的分配表一一对应。
 *  【纪律】新颜色必须: 先在表里认领槽位 -> 这里加 define -> 才能在代码用。
 *  渐变段 (1-15, 16-31) 只保证"段内索引越大越深/越暖"的相对语义,
 *  具体档位含义由绘制代码解释。
 * ===================================================================== */

#define PAL_SKY      1    /* 1..15  天空垂直渐变: 深靛 -> 暖橙 */
#define PAL_SAND_L   16   /* 16..23 后排沙丘: 亮 -> 暗 */
#define PAL_SAND_D   24   /* 24..31 前排沙丘: 暗 -> 更暗 */
#define PAL_SUN_CORE 32   /* 日核 (金) */
#define PAL_SUN_EDGE 33   /* 日缘 (橙) */
#define PAL_STAR     34   /* 星 (冷白) */
#define PAL_TITLE    35   /* 标题/正文 (米白) */
#define PAL_SHADOW   36   /* 标题投影 (近黑) */
#define PAL_CON_BG   37   /* 控制台底色 (深靛灰) */
#define PAL_CON_TINT 38   /* 控制台强调 (青) */
#define PAL_CON_WARN 39   /* 控制台警示 (琥珀) */
#define PAL_PANIC    40   /* 异常红屏 */
#define PAL_WIN_TITLE 41  /* 窗口标题栏 (未聚焦灰) */
#define PAL_TASKBAR  42   /* 任务栏底色 (深灰蓝) */

#endif /* SANDCORE_PALETTE_H */
