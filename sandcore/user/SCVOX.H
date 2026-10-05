#ifndef SANDCORE_VOXEL_LIBRARY_H
#define SANDCORE_VOXEL_LIBRARY_H
#include "SCENE.H"
/* =====================================================================
 * mio：通用整数体素查询源库v1；先声明合同，再由SCVOX.inc实现。
 *
 * 应用拥有cells、chunks、上下文及其生命周期。库不申请内存、不读取
 * 文件、不提交窗口、不解释材料；0为空，其余所有u8值都是可命中物。
 * 线性布局是(y*depth+z)*width+x，每格256个Q8位置单位。宽/高/深
 * 各1..128，调用者保证cells至少width*height*depth字节。chunks是
 * u16占用计数数组，至少ceil(w/8)*ceil(h/8)*ceil(d/8)项；每项最多
 * 512，不使用饱和标志，故删除最后一个非空体素后也能立即恢复空块。
 *
 * bind不扫描世界；rebuild完整扫描后才发布valid。write按线性索引
 * 同步维护有效占用计数。直接批量改cells前先invalidate，之后可
 * rebuild；未重建的查询自动走原逐格路径，不使用陈旧空间索引。
 * 尺寸/数组/工作区变化须重新bind。字段由这些函数维护，调用者不
 * 随意修改上下文元数据；工作区在整个查询期间必须保持存活。
 *
 * 查询起点Q8、方向为非零整数向量，各分量绝对值<=8192，距离
 * 1..16000 Q8。内部t为Q16；同距按X、Y、Z优先跨轴。最多检查192
 * 个体素，与Sand World原DDA一致。skip仅跳过一个索引的实体，
 * previous仍只记录最后真正的空气格，不能把玻璃/水或skip实体
 * 当成可建造空气。命中提供完整格号/材料/前格/距离/位置/外法线。
 * 起点所在实体在t<=1时忽略，保留旧自交约定。未命中只定义found=0、
 * index=previous=-1，其余字段不伪造命中内容。坏参数返回-1。
 *
 * 空块加速跳过的是同一组整数DDA事件：不改变delta截零、事件顺序、
 * 射线、阴影或材料。不是低分辨率渲染/近似步进/最终颜色缓存。
 * 使用方式：同一翻译单元先包含SCENE.inc，再包含SCVOX.inc。
 * 仅依赖SCENE的整数长度/比值/位移函数，适用于SCCC和宿主诊断。
 * ===================================================================== */
#define SCV_VERSION 1
#define SCV_OK 0
#define SCV_INVALID -1
typedef struct {
    int version,width,height,depth,count,chunk_width,chunk_height,chunk_depth,chunk_count,valid;
    u8 *cells;
    u16 *chunks;
} ScvGrid;
typedef struct {int found,index,previous,kind,distance;ScnVec p,n;} ScvHit;
static int scv_bind(ScvGrid *grid,u8 *cells,int width,int height,int depth,u16 *chunks,int capacity);
static void scv_invalidate(ScvGrid *grid);
static int scv_rebuild(ScvGrid *grid);
static int scv_write(ScvGrid *grid,int index,int kind);
static int scv_query(ScvGrid *grid,ScnVec *origin,ScnVec *direction,int distance,int skip,ScvHit *hit);
#endif
