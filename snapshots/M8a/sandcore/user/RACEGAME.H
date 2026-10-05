#ifndef SANDCORE_RACE_GAME_H
#define SANDCORE_RACE_GAME_H
/* mio：DUNE RUN内容层私有接口。赛事状态/记录与SCENE车体渲染分开，
 * AI和玩家共用4tick仿真步长，HUD名次来自真实里程，不能把动画车
 * 算作对手。旧12000单位自由跑/4B最佳圈记录仍可选择legacy模式。 */
#define RACE_RIVALS 5
static void reset(void);
static int race_length(void);
static void race_restart(void);
static void race_rivals_step(int before);
static void race_complete_lap(void);
static void race_records_load(void);
static int race_driver_at(int place);
static void race_content_draw(void);
static void race_menu_draw(void);
static void race_content_key(int key);
static void race_content_action(int action);
#endif
