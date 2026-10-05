#ifndef SANDCORE_WORLD_GAME_H
#define SANDCORE_WORLD_GAME_H
/* mio：SAND WORLD的玩法层，世界生成/存档/事务/界面与射线分开。
 * 这是本游戏私有源接口，不是SCAPI或内核ABI。旧24x12x24创作模式
 * 与SBOX1MIO保留；新远征/创造使用独立64x24x64世界及SWLD2MIO。
 * 库函数先声明，world.c提供唯一体素数组/玩家位置与cell访问。
 * 任何采集/合成/修复失败必须原子拒绝，不能丢物品或修改世界。 */
#define WORLD2_HEADER 256
#define WORLD_KINDS 16
static void world_new_generate(void);
static void world_new_save(void);
static int world_new_load(void);
static int world_harvest(int index);
static int world_place(int index);
static int world_activate(int index);
static int world_craft(int recipe);
static void world_progress_tick(void);
static void world_overlay(void);
static void world_game_key(int key);
static void world_game_action(int action);
static void world_enemies_step(void);
static void world_enemies_clear(void);
static int world_enemy_attack(void);
static void world_enemies_draw(void);
static int world_body_clear(int x,int y,int z);
static void world_panel_open(int panel);
static void save_world(void);
static int load_world(void);
#endif
