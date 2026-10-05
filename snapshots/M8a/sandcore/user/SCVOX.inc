/* =====================================================================
 * mio：整数DDA空块跳跃。正确性关键是保持“事件序列”，不是重新以
 * 浮点射线/AABB求一处大步交点。原delta=floor(256*65536/|dir|)，
 * 因而离相机远时其整数累积值与理想几何略有不同；重新算块边界
 * 会悄悄换舍入、同距优先级、表面UV及建造previous。这里直接合并
 * 原来的等差事件，起点/命中/法线与逐格版本完全一致。
 * ===================================================================== */
#include "SCVOX.H"
static int scv_bind(ScvGrid *grid,u8 *cells,int width,int height,int depth,u16 *chunks,int capacity)
{
    if(!grid||!cells||!chunks||width<1||width>128||height<1||height>128||depth<1||depth>128)return SCV_INVALID;
    int cw=(width+7)/8,ch=(height+7)/8,cd=(depth+7)/8,total=cw*ch*cd;
    if(capacity<total)return SCV_INVALID;
    /* 验证全部容量后才换绑定；失败保留原上下文，不把半个新尺寸
     * 和旧工作区拼成可使用的索引。没有隐藏清空调用者的体素。 */
    grid->version=SCV_VERSION;grid->width=width;grid->height=height;grid->depth=depth;
    grid->count=width*height*depth;grid->chunk_width=cw;grid->chunk_height=ch;
    grid->chunk_depth=cd;grid->chunk_count=total;grid->cells=cells;grid->chunks=chunks;grid->valid=0;
    return SCV_OK;
}
static void scv_invalidate(ScvGrid *grid){if(grid)grid->valid=0;}
static int scv_rebuild(ScvGrid *grid)
{
    if(!grid||grid->version!=SCV_VERSION||!grid->cells||!grid->chunks)return SCV_INVALID;
    grid->valid=0;
    for(int i=0;i<grid->chunk_count;i++)grid->chunks[i]=0;
    int index=0;
    for(int y=0;y<grid->height;y++)for(int z=0;z<grid->depth;z++){
        int row=((y>>3)*grid->chunk_depth+(z>>3))*grid->chunk_width;
        for(int x=0;x<grid->width;x++){
            if(grid->cells[index])grid->chunks[row+(x>>3)]++;
            index++;
        }
    }
    /* 最后才发布有效位；未完成的扫描绝不能被当作“空世界”。本
     * 源库同步运行于调用者任务，没有跨进程共享或额外内核状态。 */
    grid->valid=1;return SCV_OK;
}
static int scv_write(ScvGrid *grid,int index,int kind)
{
    if(!grid||grid->version!=SCV_VERSION||!grid->cells||(u32)index>=(u32)grid->count||(u32)kind>255u)return SCV_INVALID;
    int old=grid->cells[index];
    if(grid->valid&&(!old!=(kind==0))){
        int x=index%grid->width,z=(index/grid->width)%grid->depth,y=index/(grid->width*grid->depth);
        int chunk=((y>>3)*grid->chunk_depth+(z>>3))*grid->chunk_width+(x>>3);
        if(kind)grid->chunks[chunk]++;else grid->chunks[chunk]--;
    }
    /* 无效索引也允许合法编辑，但不把未扫描的计数错误地恢复有效；
     * 下一次rebuild仍按真实cells重建。非零材料互换不影响占用。 */
    grid->cells[index]=(u8)kind;return SCV_OK;
}
static int scv_events(int first,int delta,int target,int inclusive)
{
    /* 求同轴等差事件在target之前（或含同距）出现的次数。零方向
     * 的first为0x3FFFFFFF，而合法距离的最大参数不足该值，因此
     * 在入口返回0，不会用这个哨兵做加法或乘8而溢出。 */
    if(first>target||(!inclusive&&first==target))return 0;
    return (target-first-(inclusive?0:1))/delta+1;
}
static int scv_query(ScvGrid *grid,ScnVec *origin,ScnVec *direction,int distance,int skip,ScvHit *hit)
{
    if(!hit)return SCV_INVALID;
    hit->found=0;hit->index=hit->previous=-1;
    if(!grid||grid->version!=SCV_VERSION||!grid->cells||!origin||!direction||distance<1||distance>16000
        ||direction->x<-8192||direction->x>8192||direction->y<-8192||direction->y>8192
        ||direction->z<-8192||direction->z>8192)return SCV_INVALID;
    int ix=origin->x>>8,iy=origin->y>>8,iz=origin->z>>8;
    int width=grid->width,height=grid->height,depth=grid->depth;
    if((u32)ix>=(u32)width||(u32)iy>=(u32)height||(u32)iz>=(u32)depth)return SCV_OK;
    int vertical_stride=width*depth,index=(iy*depth+iz)*width+ix;
    int sx=direction->x<0?-1:1,sy=direction->y<0?-1:1,sz=direction->z<0?-1:1;
    int length=scn_length(direction);if(!length)return SCV_OK;
    int max_t=scn_ratio16(distance,length);
    int tx=direction->x?scn_ratio16((ix+(sx>0))*256-origin->x,direction->x):0x3FFFFFFF;
    int ty=direction->y?scn_ratio16((iy+(sy>0))*256-origin->y,direction->y):0x3FFFFFFF;
    int tz=direction->z?scn_ratio16((iz+(sz>0))*256-origin->z,direction->z):0x3FFFFFFF;
    int dx=direction->x?16777216/scn_abs(direction->x):0x3FFFFFFF;
    int dy=direction->y?16777216/scn_abs(direction->y):0x3FFFFFFF;
    int dz=direction->z?16777216/scn_abs(direction->z):0x3FFFFFFF;
    /* 索引和数组均由当前任务同步拥有，一次查询中不会被编辑；
     * 有效位只取一次。先检查当前格是否命中，再查空块索引，
     * 使近处表面命中不为一个马上结束的查询支付块定位开销。
     * 命中合同不变：包括skip实体、起点自交和最后真正空气格。 */
    int indexed=grid->valid;
    int t=0,last=-1,nx=0,ny=0,nz=0,air=-1;
    for(int step=0;step<192&&t<=max_t;step++){
        if((u32)ix>=(u32)width||(u32)iy>=(u32)height||(u32)iz>=(u32)depth)return SCV_OK;
        int kind=grid->cells[index];
        if(kind&&index!=skip&&t>1){
            hit->found=1;hit->index=index;hit->previous=last;hit->kind=kind;hit->distance=scn_displace(length,t);
            scn_set(&hit->p,origin->x+scn_displace(direction->x,t),origin->y+scn_displace(direction->y,t),origin->z+scn_displace(direction->z,t));
            scn_set(&hit->n,nx,ny,nz);return SCV_OK;
        }
        if(!kind)last=index;
        if(indexed&&air<0){
            int chunk=((iy>>3)*grid->chunk_depth+(iz>>3))*grid->chunk_width+(ix>>3);
            air=grid->chunks[chunk]==0;
        }
        if(indexed&&air){
            /* 当前块每个有效单元均为空。先算到各轴块边界还需跨
             * 多少个原事件；非8整倍尺寸的末块必须同时裁到真实
             * 世界边界。24×12×24 legacy的上块只到Y12，不能
             * 跳到Y16后再误把别行地址当作体素。 */
            int ax=sx>0?8-(ix&7):(ix&7)+1,ay=sy>0?8-(iy&7):(iy&7)+1,az=sz>0?8-(iz&7):(iz&7)+1;
            if(sx>0&&ax>width-ix)ax=width-ix;
            if(sy>0&&ay>height-iy)ay=height-iy;
            if(sz>0&&az>depth-iz)az=depth-iz;
            int bx=direction->x?tx+(ax-1)*dx:0x3FFFFFFF;
            int by=direction->y?ty+(ay-1)*dy:0x3FFFFFFF;
            int bz=direction->z?tz+(az-1)*dz:0x3FFFFFFF;
            int exit=bx,axis=1;if(by<exit){exit=by;axis=2;}if(bz<exit){exit=bz;axis=3;}
            if(exit>max_t)return SCV_OK;
            /* 同距按X/Y/Z依次跨轴。若首次离块发生在Y轴，含同距
             * X事件但不含Z事件；重心/法线/previous因此保持逐格
             * 顺序。不能把三个等距轴一次全跨到对角格。 */
            int kx=scv_events(tx,dx,exit,axis>=1),ky=scv_events(ty,dy,exit,axis>=2),kz=scv_events(tz,dz,exit,axis>=3);
            int crossed=kx+ky+kz;
            if(crossed>0&&crossed<=191-step){
                index+=sx*kx+sy*ky*vertical_stride+sz*kz*width;
                ix+=sx*kx;iy+=sy*ky;iz+=sz*kz;
                tx+=kx*dx;ty+=ky*dy;tz+=kz*dz;t=exit;
                nx=ny=nz=0;
                if(axis==1){last=index-sx;nx=-sx*256;}
                else if(axis==2){last=index-sy*vertical_stride;ny=-sy*256;}
                else{last=index-sz*width;nz=-sz*256;}
                step+=crossed-1;air=-1;continue;
            }
            /* 距离/192步上限没有放宽。接近步数边界时仍沿原事件
             * 逐步结束，不能跳过本不允许访问的单元再报告命中。 */
        }
        nx=ny=nz=0;
        if(tx<=ty&&tx<=tz){t=tx;tx+=dx;ix+=sx;index+=sx;nx=-sx*256;if((ix&7)==(sx>0?0:7))air=-1;}
        else if(ty<=tz){t=ty;ty+=dy;iy+=sy;index+=sy*vertical_stride;ny=-sy*256;if((iy&7)==(sy>0?0:7))air=-1;}
        else{t=tz;tz+=dz;iz+=sz;index+=sz*width;nz=-sz*256;if((iz&7)==(sz>0?0:7))air=-1;}
    }
    return SCV_OK;
}
