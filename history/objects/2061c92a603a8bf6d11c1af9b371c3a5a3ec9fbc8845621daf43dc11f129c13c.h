#ifndef SANDCORE_SCENE_LIBRARY_H
#define SANDCORE_SCENE_LIBRARY_H
#include "SCAPI.H"
/* mio：三环场景库v1，源级接口，不是新内核ABI。先声明再实现。
 * 调用者拥有上下文、物体数组、像素内存；库不创建窗口、不隐藏分配。
 * 世界坐标/法线Q8，射线参数t为Q16，颜色RGB8，输出不透明ARGB32。
 * 相机射线保留焦距幅值，不强制单位化；trace按向量长度换算远距/雾。
 * intersect的limit与hit.t同为Q16参数，真实距离=hit.t*方向长度/65536。
 * SCCC不支持结构体
 * 按值传递，故向量/命中统一传指针，函数结果只用整数与RGB。
 * bind可切换独立场景；已绑定数组至少capacity项，1..4096。
 * 增加物体返回索引或负错误，场景满/几何错误不改变已有物体。
 * draw_rows可分批渲染同一相机帧，应用在批间处理输入与任务让出。
 * 批次只写[row_begin,row_end)，不会把整帧变成隐含发布操作。 */
#define SCN_VERSION 1
#define SCN_OK 0
#define SCN_INVALID -1
#define SCN_FULL -2
#define SCN_COORD_LIMIT 8192
typedef struct {int x,y,z;} ScnVec;
typedef struct {int kind;ScnVec low,high,third;int radius;u32 color;int reflection,texture;} ScnObject;
typedef struct {int found,index,t;ScnVec p,n;} ScnHit;
typedef struct {
    int version,count,capacity,error,floor,floor_reflection;
    u32 floor_color,sky_top,horizon,sun_color;
    ScnVec eye,forward,right,up,sun;
    ScnObject *objects;
    int far_clip,max_bounces,ambient,fog_distance;
} ScnScene;
/* 可选主射线批次工作区：由调用者拥有，每物体一项。
 * prepare以当前几何/主射线起点建立精确三角形常量。批间直接编辑
 * 公开物体数组必须重新prepare；旧调用者没有启用缓存时行为不变。
 * 库的bind/reset/add会主动使缓存失效，不隐含分配或扩大ScnScene。 */
typedef struct {ScnVec e1,e2,offset,cross,normal;int plane;} ScnPrepared;
#define SCN_HAS_PREPARED_PRIMARY 1
static int scn_prepare_primary(ScnVec *origin,ScnPrepared *workspace,int capacity);
static int scn_bind(ScnScene *scene,ScnObject *objects,int capacity);
static int scn_use(ScnScene *scene);
static void scn_reset(void);
static void scn_camera(int x,int y,int z,int tx,int ty,int tz);
static void scn_camera_ray(ScnVec *out,int x,int y,int width,int height,int focal);
static int scn_sphere(int x,int y,int z,int radius,u32 color,int reflection,int texture);
static int scn_box(int x,int y,int z,int xx,int yy,int zz,u32 color,int reflection,int texture);
static int scn_triangle(ScnVec *a,ScnVec *b,ScnVec *c,u32 color,int reflection);
static void scn_intersect(ScnVec *origin,ScnVec *direction,ScnHit *hit,int limit);
/* 只查询有无遮蔽物，不生成最近点/法线。边界与intersect的found相同，
 * 普通颜色/透射需要最近点时仍用原入口；这是源库追加能力，无内核ABI。 */
static int scn_occluded(ScnVec *origin,ScnVec *direction,int limit);
static u32 scn_trace(ScnVec *origin,ScnVec *direction);
static int scn_draw_rows(u32 *pixels,int width,int height,int stride,int focal,int row_begin,int row_end);
#endif
