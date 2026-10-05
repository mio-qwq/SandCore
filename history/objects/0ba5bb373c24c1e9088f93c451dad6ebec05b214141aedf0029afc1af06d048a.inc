/* =====================================================================
 * mio：M8三环真彩场景基础。材质端点先登记在GFX.md，不修改DAC。
 * 世界位置/法线使用Q8，一米或一个体素=256；射线参数为Q16。
 * 射线求交、法线、遮挡、反射全部在当前进程执行，FRAME32仅提交
 * 最终图像。没有宿主图片、视频帧、浮点或libc参与这些场景。
 *
 * 有界场景约束：坐标绝对值<=8192，半径<=2048；调用者提供物体数组。
 * 不按像素申请内存，不递归反射；每条主射线最多两个反射交点，
 * 阴影单独查询最近遮挡。窗口字形/控件仍由NUI按原生像素绘制。
 * ===================================================================== */
#include "SCENE.H"
#define SCN_SKY_TOP 0x456F94u
#define SCN_HORIZON 0xC8DDE6u
#define SCN_SUN 0xFFF1D6u
#define SCN_SAND 0xB5A07Du
#define SCN_STONE 0xAFA694u
#define SCN_ASPHALT 0x373D45u
#define SCN_GRASS 0x667341u
#define SCN_OAK 0x806B51u
#define SCN_CERAMIC 0xD5D8DCu
#define SCN_METAL 0x78828Cu
#define SCN_RUBBER 0x23272Au
#define SCN_GLASS 0x9DD2D8u
#define SCN_PAINT 0xB74C45u
#define SCN_IVORY 0xF4EEE2u
#define SCN_SHADE 0x172536u
#define SCN_SEA 0x467D8Cu
#define SCN_RED 0xED3548u
#define SCN_ORANGE 0xFF8B35u
#define SCN_YELLOW 0xF4DD45u
#define SCN_GREEN 0x58BD73u
#define SCN_CYAN 0x4BC9DBu
#define SCN_BLUE 0x497DDDu
#define SCN_VIOLET 0xA86BD4u
static ScnObject scn_default_objects[32];
static const ScnScene scn_initial={1,0,32,0,1,28,SCN_STONE,SCN_SKY_TOP,SCN_HORIZON,SCN_SUN,
    {0,256,-768},{0,0,256},{256,0,0},{0,256,0},{-115,205,102},scn_default_objects,16000,3,92,96};
static ScnScene scn_default={1,0,32,0,1,28,SCN_STONE,SCN_SKY_TOP,SCN_HORIZON,SCN_SUN,
    {0,256,-768},{0,0,256},{256,0,0},{0,256,0},{-115,205,102},scn_default_objects,16000,3,92,96};
static ScnScene *scn_active=&scn_default;
static ScnPrepared *scn_prepared;
static ScnScene *scn_prepared_scene;
static ScnObject *scn_prepared_objects;
static ScnVec scn_prepared_origin;
static int scn_prepared_count,scn_prepared_valid;
#define scn_objects (scn_active->objects)
#define scn_count (scn_active->count)
#define scn_floor (scn_active->floor)
#define scn_floor_reflection (scn_active->floor_reflection)
#define scn_floor_color (scn_active->floor_color)
#define scn_eye (scn_active->eye)
#define scn_forward (scn_active->forward)
#define scn_right (scn_active->right)
#define scn_up (scn_active->up)
static int scn_clamp(int x,int lo,int hi){return x<lo?lo:x>hi?hi:x;}
static int scn_abs(int x){return x<0?-x:x;}
static int scn_tick,scn_frame_ticks,scn_render_width,scn_render_height;
/* mio：同一场景的太阳方向在一帧数百万射线中保持不变。
 * 缓存的是精确整数归一化结果，不改变光源、反射或射线数量。
 * 每次使用都检查上下文身份及三个原始分量，调用者直接修改sun
 * 或切换独立场景也会立即失效，不依赖应用额外通知。 */
static ScnScene *scn_sun_scene;
static ScnVec scn_sun_input,scn_sun_unit;
static int scn_sun_length;
static int scn_valid_point(int x,int y,int z)
{return x>=-8192&&x<=8192&&y>=-8192&&y<=8192&&z>=-8192&&z<=8192;}
static int scn_isqrt(u32 value)
{
    u32 result=0,bit=1u<<30;
    while(bit>value)bit>>=2;
    while(bit){if(value>=result+bit){value-=result+bit;result=(result>>1)+bit;}else result>>=1;bit>>=2;}
    return (int)result;
}
static void scn_set(ScnVec *v,int x,int y,int z){v->x=x;v->y=y;v->z=z;}
static void scn_unit(ScnVec *v)
{
    /* 大三角形叉积可能远大于坐标界。先等比例缩小再平方，
     * 保持法线方向，避免单位化自身在32位平方和上溢出。 */
    while(v->x<-8192||v->x>8192||v->y<-8192||v->y>8192||v->z<-8192||v->z>8192){v->x/=2;v->y/=2;v->z/=2;}
    int size=scn_isqrt((u32)(v->x*v->x+v->y*v->y+v->z*v->z));
    if(!size){scn_set(v,0,0,256);return;}
    v->x=v->x*256/size;v->y=v->y*256/size;v->z=v->z*256/size;
}
static void scn_sun_vector(ScnVec *out)
{
    if(scn_sun_scene!=scn_active||scn_sun_input.x!=scn_active->sun.x
        ||scn_sun_input.y!=scn_active->sun.y||scn_sun_input.z!=scn_active->sun.z){
        scn_sun_scene=scn_active;scn_sun_input=scn_active->sun;
        scn_sun_unit=scn_sun_input;scn_unit(&scn_sun_unit);
        scn_sun_length=scn_isqrt((u32)(scn_sun_unit.x*scn_sun_unit.x+
            scn_sun_unit.y*scn_sun_unit.y+scn_sun_unit.z*scn_sun_unit.z));
    }
    *out=scn_sun_unit;
}
static int scn_product(int a,int b)
{
    /* mio：逐射线叉积/法线/光照绝大多数分量位于有符号16位范围。
     * 两因子先各自限界，最大完整乘积32768*32768=1073741824，
     * 严格小于INT_MAX，因此可先乘后向零除256。它与原来的
     * 整数/余数拆分逐项完全相等：拆出的各项都与完整乘积同号，
     * 只有最后不足一个Q8单位的余数被截掉，没有改变负数舍入。
     *
     * 原拆分每个乘积重复求两次商、三次余数并走表达式栈；点积
     * 三轴、叉积六项、各交点/各物体又重复这一成本。把常见的
     * 有界数据走一个乘法和一次常数除法，当前SCCC还能生成精确
     * 带偏置的算术移位。阈值判断不取abs(INT_MIN)，避免取绝对值
     * 自身溢出。超界叉积仍保持原拆分，不把场景坐标限制误当成
     * 中间向量范围，也不引入浮点、64位运行库或近似倒数。 */
    if(a>=-32768&&a<=32767&&b>=-32768&&b<=32767)return a*b/256;
    return (a/256)*b+(a%256)*(b/256)+(a%256)*(b%256)/256;
}
static int scn_dot(ScnVec *a,ScnVec *b)
{
    /* 必须逐轴截零后相加，不能改成三乘积之和再除256；后一种
     * 会改变不足一个单位的分量抵消，破坏既有交点与整帧等价。 */
    return scn_product(a->x,b->x)+scn_product(a->y,b->y)+scn_product(a->z,b->z);
}
static void scn_cross(ScnVec *out,ScnVec *a,ScnVec *b)
{scn_set(out,scn_product(a->y,b->z)-scn_product(a->z,b->y),scn_product(a->z,b->x)-scn_product(a->x,b->z),scn_product(a->x,b->y)-scn_product(a->y,b->x));}
static int scn_fraction_ratio(int numerator,int denominator,int bits)
{
    /* 精确的32位有符号定点比值：先整数除法，再逐位生成bits位
     * 小数。不形成numerator左移，宽三角形/远车的比值不会溢出。
     * 使用无符号幅值也覆盖INT_MIN；超出i32时饱和，后续距离界限
     * 会丢弃该交点，不让溢出回绕成镜头前的幽灵几何。 */
    if(!denominator)return numerator<0?-2147483647:2147483647;
    int negative=(numerator<0)!=(denominator<0);
    u32 n=numerator<0?0u-(u32)numerator:(u32)numerator;
    u32 d=denominator<0?0u-(u32)denominator:(u32)denominator;
    /* 绝大多数盒面/体素/法线的幅值可在32位内直接左移。
     * 先证明不溢出再做一次无符号除法，与下面逐位小数算法
     * 完全相等；宽三角形/INT_MIN等仍用原饱和路径。
     * 不能无条件n<<bits，也不能用近似倒数改变细射线交点。 */
    if(n<=(2147483647u>>bits)){
        u32 scaled=(n<<bits)/d;return negative?-(int)scaled:(int)scaled;
    }
    if(bits==8||bits==16){
        /* mio：i386的DIV本来就接受EDX:EAX这一对64位被除数；
         * 它不需要FPU/SIMD，不需要给SCCC增加long long或运行库。
         * n<<bits的低32位放EAX，高位由n>>(32-bits)放EDX，
         * 固定寄存器约束让GCC和系统内SCCC都执行同一条整数DIV。
         *
         * 必须先检查高字<除数：否则商超过32位，CPU会产生#DE，
         * 原来的饱和语义却要求返回±INT_MAX。高字不小于除数
         * 时肯定需要饱和；允许DIV后还需把32位无符号商夹到
         * INT_MAX。分母为零已在入口处理，INT_MIN幅值仍用u32。
         * 原逐位算法在大三角形每次求交生成8/16轮循环；现在
         * 精确硬件整数除法一次完成，不改变一位结果或几何质量。
         * 其余bits保留原通用分数路径，旧内部使用约定不缩减。 */
        u32 low=n<<bits,high=n>>(32-bits);
        if(high>=d)return negative?-2147483647:2147483647;
        __asm__ __volatile__("div %%ecx" : "+a"(low), "+d"(high) : "c"(d) : "cc");
        if(low>2147483647u)low=2147483647u;
        return negative?-(int)low:(int)low;
    }
    u32 whole=n/d,remainder=n%d,fraction=0;
    if(whole>(2147483647u>>bits))return negative?-2147483647:2147483647;
    for(int i=0;i<bits;i++){remainder<<=1;fraction<<=1;if(remainder>=d){remainder-=d;fraction++;}}
    u32 result=(whole<<bits)|fraction;return negative?-(int)result:(int)result;
}
static int scn_ratio(int n,int d){return scn_fraction_ratio(n,d,8);}
static int scn_ratio16(int n,int d){return scn_fraction_ratio(n,d,16);}
static int scn_displace(int component,int parameter)
{
    /* Q16参数也可能在远距/近平行时很大，不能先乘再除65536。
     * 拆出整数与余数后单轴±8192仍在i32内；符号余数保持C截零
     * 约定，同一射线的球/盒/地面与雾都使用相同换算，不跳像素。 */
    return (parameter/65536)*component+(parameter%65536)*component/65536;
}
static int scn_length(ScnVec *v){return scn_isqrt((u32)(v->x*v->x+v->y*v->y+v->z*v->z));}
static int scn_sin(int phase)
{
    /* Bhaskara半周期近似，范围固定，避免大查表和浮点三角函数。
     * 周期1024与旧世界存档yaw一致，恢复无需角度格式迁移。 */
    phase&=1023;int sign=1;if(phase>=512){phase-=512;sign=-1;}
    int product=phase*(512-phase);
    return sign*(product*16)/(5*1024-4*product/256);
}
static int scn_cos(int phase){return scn_sin(phase+256);}
static u32 scn_mix(u32 a,u32 b,int weight)
{
    weight=scn_clamp(weight,0,256);int left=256-weight;
    u32 r=(((a>>16)&255)*left+((b>>16)&255)*weight)>>8;
    u32 g=(((a>>8)&255)*left+((b>>8)&255)*weight)>>8;
    u32 v=((a&255)*left+(b&255)*weight)>>8;return (r<<16)|(g<<8)|v;
}
static u32 scn_light(u32 c,int amount)
{
    int r=(int)((c>>16)&255)*amount/256,g=(int)((c>>8)&255)*amount/256,b=(int)(c&255)*amount/256;
    return (u32)(scn_clamp(r,0,255)<<16)|(u32)(scn_clamp(g,0,255)<<8)|(u32)scn_clamp(b,0,255);
}
static u32 scn_hash(int x,int y,int z)
{u32 h=(u32)x*374761393u+(u32)y*668265263u+(u32)z*2246822519u;h=(h^(h>>13))*1274126177u;return h^(h>>16);}
static int scn_noise(int x,int z,int cell)
{
    /* 平滑双线性值噪声；负坐标先平移到场景界内正值，纹理不随镜头
     * 移动。近处细节用坐标哈希，远处不混入逐帧随机闪烁。 */
    if(cell<1||cell>32768)return 128;
    x=(int)((u32)x&65535u);z=(int)((u32)z&65535u);int ix=x/cell,iz=z/cell,fx=(x%cell)*256/cell,fz=(z%cell)*256/cell;
    fx=fx*fx*(768-2*fx)/65536;fz=fz*fz*(768-2*fz)/65536;
    int a=(int)(scn_hash(ix,0,iz)&255),b=(int)(scn_hash(ix+1,0,iz)&255);
    int c=(int)(scn_hash(ix,0,iz+1)&255),d=(int)(scn_hash(ix+1,0,iz+1)&255);
    int top=(a*(256-fx)+b*fx)/256,bottom=(c*(256-fx)+d*fx)/256;
    return (top*(256-fz)+bottom*fz)/256;
}
static u32 scn_sky(ScnVec *direction)
{
    ScnVec normalized;scn_set(&normalized,direction->x,direction->y,direction->z);scn_unit(&normalized);direction=&normalized;
    int height=scn_clamp(direction->y,0,256);
    u32 color=scn_mix(scn_active->horizon,scn_active->sky_top,height);
    /* 日盘方向固定于世界，镜头转动时随透视移动；柔和日晕具有
     * 连续角度衰减，不是一张画在屏幕固定位置的太阳贴图。 */
    ScnVec sun;scn_sun_vector(&sun);
    int alignment=scn_dot(direction,&sun);
    if(alignment>210)color=scn_mix(color,scn_active->sun_color,(alignment-210)*(alignment-210)/10);
    if(direction->y>16&&direction->y<180){
        int cloud=scn_noise(direction->x*10,direction->z*10+direction->y*4,256);
        cloud=scn_clamp((cloud-156)*3,0,120);color=scn_mix(color,SCN_IVORY,cloud);
    }
    return color;
}
static void scn_camera(int x,int y,int z,int tx,int ty,int tz)
{
    if(!scn_valid_point(x,y,z)||!scn_valid_point(tx,ty,tz)||(x==tx&&y==ty&&z==tz)){scn_active->error=SCN_INVALID;return;}
    scn_set(&scn_eye,x,y,z);scn_set(&scn_forward,tx-x,ty-y,tz-z);scn_unit(&scn_forward);
    scn_set(&scn_right,scn_forward.z,0,-scn_forward.x);
    /* 正上/正下看时水平投影为零，明确选择X轴为右向，不能把零向量
     * 的默认Z轴当成右向，否则它与视线重合、整个画面塌成一条线。 */
    if(!scn_right.x&&!scn_right.z)scn_set(&scn_right,256,0,0);
    else scn_unit(&scn_right);
    scn_set(&scn_up,-scn_forward.y*scn_right.z/256,
        (scn_forward.z*scn_right.x-scn_forward.x*scn_right.z)/256,scn_forward.y*scn_right.x/256);
    scn_unit(&scn_up);
}
static void scn_camera_ray(ScnVec *out,int x,int y,int width,int height,int focal)
{
    if(focal<1||focal>4096||width<1||height<1||width>1920||height>1080){scn_set(out,0,0,256);scn_active->error=SCN_INVALID;return;}
    /* 保留像素坐标与焦距的整数比例，不先把xx舍成1/256角度。
     * 原来的单位Q8方向会使1080p多列共用一条射线，尽管每个像素
     * 都执行了循环，看起来仍像低分辨率放大。求交用Q16参数，
     * 同时保留细射线方向和交点位置；颜色/法线仍为稳定Q8。 */
    int xx=2*x+1-width,yy=height-2*y-1;
    scn_set(out,(scn_forward.x*focal*2+scn_right.x*xx+scn_up.x*yy)/512,
        (scn_forward.y*focal*2+scn_right.y*xx+scn_up.y*yy)/512,
        (scn_forward.z*focal*2+scn_right.z*xx+scn_up.z*yy)/512);
    if(!out->x&&!out->y&&!out->z)scn_set(out,scn_forward.x,scn_forward.y,scn_forward.z);
    if(!out->x&&!out->y&&!out->z){scn_set(out,0,0,256);scn_active->error=SCN_INVALID;return;}
    /* floor(sqrt(sum))<256等价于sum<65536，省去每条相机射线
     * 一次整数开方。仍保留完全相同向量和原生逐像素方向精度。 */
    while((u32)(out->x*out->x+out->y*out->y+out->z*out->z)<65536u){out->x*=2;out->y*=2;out->z*=2;}
}
static int scn_bind(ScnScene *scene,ScnObject *objects,int capacity)
{
    if(!scene||!objects||capacity<1||capacity>4096)return SCN_INVALID;
    *scene=scn_initial;scene->objects=objects;scene->count=0;scene->capacity=capacity;scene->error=0;
    scn_active=scene;scn_prepared_valid=0;return SCN_OK;
}
static int scn_use(ScnScene *scene)
{
    /* 切回已有场景不能重置其物体/镜头。bind仅初始化一次，use可
     * 在两个独立视图间切换；坏上下文不替换当前有效场景。 */
    if(!scene || scene->version!=SCN_VERSION || !scene->objects || scene->capacity<1
        || scene->capacity>4096 || scene->count<0 || scene->count>scene->capacity)return SCN_INVALID;
    scn_active=scene;return SCN_OK;
}
static void scn_reset(void){scn_count=0;scn_prepared_valid=0;scn_active->error=0;scn_floor=1;scn_floor_reflection=28;scn_floor_color=SCN_STONE;}
static int scn_sphere(int x,int y,int z,int radius,u32 color,int reflection,int texture)
{
    if(!scn_valid_point(x,y,z)||radius<1||radius>2048){scn_active->error=SCN_INVALID;return SCN_INVALID;}
    if(scn_count>=scn_active->capacity){scn_active->error=SCN_FULL;return SCN_FULL;}ScnObject *o=&scn_objects[scn_count++];
    o->kind=1;scn_set(&o->low,x,y,z);o->radius=radius;o->color=color;o->reflection=reflection;o->texture=texture;
    scn_prepared_valid=0;return scn_count-1;
}
static int scn_box(int x,int y,int z,int xx,int yy,int zz,u32 color,int reflection,int texture)
{
    if(!scn_valid_point(x,y,z)||!scn_valid_point(xx,yy,zz)||x>=xx||y>=yy||z>=zz){scn_active->error=SCN_INVALID;return SCN_INVALID;}
    if(scn_count>=scn_active->capacity){scn_active->error=SCN_FULL;return SCN_FULL;}ScnObject *o=&scn_objects[scn_count++];
    o->kind=2;scn_set(&o->low,x,y,z);scn_set(&o->high,xx,yy,zz);o->color=color;o->reflection=reflection;o->texture=texture;
    scn_prepared_valid=0;return scn_count-1;
}
static int scn_triangle(ScnVec *a,ScnVec *b,ScnVec *c,u32 color,int reflection)
{
    if(!a||!b||!c||!scn_valid_point(a->x,a->y,a->z)||!scn_valid_point(b->x,b->y,b->z)||!scn_valid_point(c->x,c->y,c->z)){
        scn_active->error=SCN_INVALID;return SCN_INVALID;
    }
    ScnVec edge1,edge2,normal;
    scn_set(&edge1,b->x-a->x,b->y-a->y,b->z-a->z);scn_set(&edge2,c->x-a->x,c->y-a->y,c->z-a->z);
    scn_cross(&normal,&edge1,&edge2);
    if(!normal.x&&!normal.y&&!normal.z){scn_active->error=SCN_INVALID;return SCN_INVALID;}
    if(scn_count>=scn_active->capacity){scn_active->error=SCN_FULL;return SCN_FULL;}ScnObject *o=&scn_objects[scn_count++];o->kind=3;
    scn_set(&o->low,a->x,a->y,a->z);scn_set(&o->high,b->x,b->y,b->z);scn_set(&o->third,c->x,c->y,c->z);
    o->color=color;o->reflection=reflection;o->texture=0;
    scn_prepared_valid=0;return scn_count-1;
}
static int scn_prepare_primary(ScnVec *origin,ScnPrepared *workspace,int capacity)
{
    /* mio：一帧的相机起点与几何相同。Moller-Trumbore原实现对每个
     * 像素、每个车身三角形重新算两条边、eye-A、叉积q、点积e2·q
     * 及命中法线；这些根本不依赖射线方向。批次先精确算一次，
     * 不近似重心、不少发射线、不缓存最终颜色或改变材质。
     *
     * 起点身份按坐标验证，所以只有真正同起点的主射线复用q等；
     * 阴影/反射的不同起点仍现算，几何边与法线可以共用。
     * 工作区明确归应用所有，长度在写之前验证。失败失效而不使用
     * 旧批次；bind/reset/add也失效。直接修改公开数组的高级调用者
     * 必须在修改后的新批次重新prepare，旧程序未启用则一直原路径。
     * 场景切换核对上下文/物体数组/数量，不把另一视图的几何用进来。 */
    scn_prepared_valid=0;
    if(!origin||!workspace||capacity<scn_count||capacity>4096)return SCN_INVALID;
    for(int i=0;i<scn_count;i++)if(scn_objects[i].kind==3){
        ScnObject *o=&scn_objects[i];ScnPrepared *p=&workspace[i];
        scn_set(&p->e1,o->high.x-o->low.x,o->high.y-o->low.y,o->high.z-o->low.z);
        scn_set(&p->e2,o->third.x-o->low.x,o->third.y-o->low.y,o->third.z-o->low.z);
        scn_set(&p->offset,origin->x-o->low.x,origin->y-o->low.y,origin->z-o->low.z);
        scn_cross(&p->cross,&p->offset,&p->e1);p->plane=scn_dot(&p->e2,&p->cross);
        scn_cross(&p->normal,&p->e1,&p->e2);scn_unit(&p->normal);
    }
    scn_prepared=workspace;scn_prepared_scene=scn_active;scn_prepared_objects=scn_objects;
    scn_prepared_count=scn_count;scn_prepared_origin=*origin;scn_prepared_valid=1;
    return SCN_OK;
}
static int scn_slab(int origin,int direction,int low,int high,int *near,int *far,int axis,int *normal,int *exit_normal)
{
    if(!direction)return origin>=low&&origin<=high;
    int a=scn_ratio16(low-origin,direction),b=scn_ratio16(high-origin,direction),n=-axis;
    if(a>b){int temp=a;a=b;b=temp;n=axis;}
    if(a>*near){*near=a;*normal=n;}if(b<*far){*far=b;*exit_normal=-n;}
    return *near<=*far&&*far>1;
}
static void scn_query(ScnVec *origin,ScnVec *direction,ScnHit *hit,int limit,int any)
{
    hit->found=0;hit->t=limit;hit->index=-2;
    /* mio：最近交点查询与“有无遮挡”共用精确的几何判据。
     * 阴影只使用found，确认一个有效交点就可以停止，不需要寻找
     * 更近物体、开方生成法线或计算世界交点。普通intersect仍扫描
     * 全部物体并保持原先严格t比较/同距先来者优先的顺序。
     * 最近点的法线最后只算一次；过去每发现更近候选都会重算，
     * 但随后常被覆盖。球的射线长度只算一次，所有球共用。
     * 不缓存几何，不要求额外失效通知，直接修改数组/切场景仍即时生效。 */
    int best_normal=0,sphere_square=0,sphere_length=0;
    int prepared=scn_prepared_valid&&scn_prepared_scene==scn_active
        &&scn_prepared_objects==scn_objects&&scn_prepared_count==scn_count;
    int primary=prepared&&origin->x==scn_prepared_origin.x&&origin->y==scn_prepared_origin.y
        &&origin->z==scn_prepared_origin.z;
    if(scn_floor&&direction->y<0){
        int t=scn_ratio16(-origin->y,direction->y);
        if(t>2&&t<hit->t){hit->found=1;hit->t=t;hit->index=-1;
            if(any)return;scn_set(&hit->n,0,256,0);}
    }
    for(int i=0;i<scn_count;i++){
        ScnObject *o=&scn_objects[i];int t=0,normal=0;
        if(o->kind==1){
            ScnVec offset;scn_set(&offset,origin->x-o->low.x,origin->y-o->low.y,origin->z-o->low.z);
            if(!sphere_square){
                sphere_square=direction->x*direction->x+direction->y*direction->y+direction->z*direction->z;
                sphere_length=scn_isqrt((u32)sphere_square);
            }
            int a=sphere_square;
            int b=offset.x*direction->x+offset.y*direction->y+offset.z*direction->z;if(a<1)continue;
            int length=sphere_length,closest=scn_ratio16(-b,a),margin=scn_ratio16(o->radius*2,length);
            if(closest<-margin || closest>hit->t+margin)continue;
            ScnVec perpendicular;scn_set(&perpendicular,offset.x+scn_displace(direction->x,closest),
                offset.y+scn_displace(direction->y,closest),offset.z+scn_displace(direction->z,closest));
            if(scn_abs(perpendicular.x)>o->radius+2 || scn_abs(perpendicular.y)>o->radius+2 || scn_abs(perpendicular.z)>o->radius+2)continue;
            int remaining=o->radius*o->radius-perpendicular.x*perpendicular.x-perpendicular.y*perpendicular.y-perpendicular.z*perpendicular.z;
            if(remaining<0)continue;
            /* 最近点法避免b*b与a*c这两个很大的数相减。世界弦长
             * 先整数开方，再以精确Q16比值变成参数，不乘大平方数。 */
            int root=scn_ratio16(scn_isqrt((u32)remaining),length);
            t=closest-root;if(t<=2)t=closest+root;
        }else if(o->kind==3){
            /* Moller-Trumbore的重心比值用Q8、射线参数用Q16。叉积除256，
             * 防止车体/建筑坐标的三次乘积溢出；零面积或近平行拒绝。 */
            ScnVec e1,e2,p,q,s;
            if(prepared){e1=scn_prepared[i].e1;e2=scn_prepared[i].e2;}
            else{
                scn_set(&e1,o->high.x-o->low.x,o->high.y-o->low.y,o->high.z-o->low.z);
                scn_set(&e2,o->third.x-o->low.x,o->third.y-o->low.y,o->third.z-o->low.z);
            }
            scn_cross(&p,direction,&e2);int det=scn_dot(&e1,&p);if(!det)continue;
            if(primary)s=scn_prepared[i].offset;
            else scn_set(&s,origin->x-o->low.x,origin->y-o->low.y,origin->z-o->low.z);
            int u=scn_ratio(scn_dot(&s,&p),det);if(u<0||u>256)continue;
            if(primary)q=scn_prepared[i].cross;else scn_cross(&q,&s,&e1);
            int v=scn_ratio(scn_dot(direction,&q),det);if(v<0||u+v>256)continue;
            t=scn_ratio16(primary?scn_prepared[i].plane:scn_dot(&e2,&q),det);
        }else{
            int far=limit,exit_normal=0;t=-2147483647;
            if(!scn_slab(origin->x,direction->x,o->low.x,o->high.x,&t,&far,1,&normal,&exit_normal)||
               !scn_slab(origin->y,direction->y,o->low.y,o->high.y,&t,&far,2,&normal,&exit_normal)||
               !scn_slab(origin->z,direction->z,o->low.z,o->high.z,&t,&far,3,&normal,&exit_normal))continue;
            /* 起点在盒内时负的入面不代表未命中。选实际出面及其外法线，
             * 支持内部相机/下一段射线，不把盒内部错误地画成天空。 */
            if(t<=2){if(!exit_normal)continue;t=far;normal=exit_normal;}
        }
        if(t<=2||t>=hit->t)continue;
        hit->found=1;hit->t=t;hit->index=i;
        if(any)return;
        best_normal=normal;
    }
    if(hit->found&&hit->index>=0){
        ScnObject *o=&scn_objects[hit->index];int t=hit->t,normal=best_normal;
        if(o->kind==1){
            scn_set(&hit->n,origin->x+scn_displace(direction->x,t)-o->low.x,
                origin->y+scn_displace(direction->y,t)-o->low.y,origin->z+scn_displace(direction->z,t)-o->low.z);scn_unit(&hit->n);
        }else if(o->kind==3){
            if(prepared)hit->n=scn_prepared[hit->index].normal;
            else{
                ScnVec e1,e2;scn_set(&e1,o->high.x-o->low.x,o->high.y-o->low.y,o->high.z-o->low.z);
                scn_set(&e2,o->third.x-o->low.x,o->third.y-o->low.y,o->third.z-o->low.z);
                scn_cross(&hit->n,&e1,&e2);scn_unit(&hit->n);
            }
            if(scn_dot(&hit->n,direction)>0){hit->n.x=-hit->n.x;hit->n.y=-hit->n.y;hit->n.z=-hit->n.z;}
        }else scn_set(&hit->n,normal==1?256:normal==-1?-256:0,
            normal==2?256:normal==-2?-256:0,normal==3?256:normal==-3?-256:0);
    }
    if(hit->found)scn_set(&hit->p,origin->x+scn_displace(direction->x,hit->t),
        origin->y+scn_displace(direction->y,hit->t),origin->z+scn_displace(direction->z,hit->t));
}
static void scn_intersect(ScnVec *origin,ScnVec *direction,ScnHit *hit,int limit)
{scn_query(origin,direction,hit,limit,0);}
static int scn_occluded(ScnVec *origin,ScnVec *direction,int limit)
{
    ScnHit hit;scn_query(origin,direction,&hit,limit,1);return hit.found;
}
static u32 scn_surface(ScnHit *hit)
{
    ScnObject *o=hit->index>=0?&scn_objects[hit->index]:0;
    u32 color=o?o->color:scn_floor_color;int texture=o?o->texture:1;
    int x=hit->p.x,z=hit->n.y?hit->p.z:hit->p.y;
    if(texture==1){int n=scn_noise(x,z,96);color=scn_light(color,226+n/5);}
    else if(texture==2){int grain=scn_noise(x,z,64)+scn_sin(x*3+scn_noise(x,z,256))/8;color=scn_light(color,214+grain/4);}
    else if(texture==3){int line=(x+32768)%256;int band=(z+32768)%128;if(line<3||band<3)color=scn_mix(color,SCN_SHADE,60);}
    return color;
}
static u32 scn_trace(ScnVec *origin,ScnVec *direction)
{
    ScnVec eye,ray;scn_set(&eye,origin->x,origin->y,origin->z);scn_set(&ray,direction->x,direction->y,direction->z);
    u32 layers[3];int reflect[3],count=0;
    int bounces=scn_clamp(scn_active->max_bounces,1,3);
    for(int bounce=0;bounce<bounces;bounce++){
        int ray_length=scn_length(&ray);if(!ray_length)ray_length=1;
        ScnHit hit;scn_intersect(&eye,&ray,&hit,scn_ratio16(scn_active->far_clip,ray_length));
        if(!hit.found){layers[count]=scn_sky(&ray);reflect[count++]=0;break;}
        ScnVec light,start;scn_sun_vector(&light);
        int diffuse=scn_clamp(scn_dot(&hit.n,&light),0,256);
        scn_set(&start,hit.p.x+hit.n.x/64,hit.p.y+hit.n.y/64,hit.p.z+hit.n.z/64);
        int shadow=scn_occluded(&start,&light,scn_ratio16(scn_active->far_clip,scn_sun_length));
        int amount=scn_active->ambient+(shadow?diffuse/7:diffuse*3/4);
        u32 color=scn_light(scn_surface(&hit),amount);
        int incidence=-scn_ratio(scn_dot(&ray,&hit.n),ray_length),reflection=hit.index<0?scn_floor_reflection:scn_objects[hit.index].reflection;
        int fresnel=scn_clamp(256-incidence,0,256);fresnel=fresnel*fresnel/256;fresnel=fresnel*fresnel/256;
        reflection=scn_clamp(reflection+fresnel*(256-reflection)/512,0,230);
        /* 直接高光与反射分开：阴影挡住直射光，但保留环境反射。
         * 反射射线从法线外4个定点单位出发，避免量化后再次击中自己。 */
        ScnVec reflected;int dot=scn_dot(&ray,&hit.n);
        scn_set(&reflected,ray.x-2*dot*hit.n.x/256,ray.y-2*dot*hit.n.y/256,ray.z-2*dot*hit.n.z/256);
        int spec=scn_clamp(scn_ratio(scn_dot(&reflected,&light),ray_length),0,256);spec=spec*spec/256;spec=spec*spec/256;spec=spec*spec/256;
        if(!shadow)color=scn_mix(color,scn_active->sun_color,spec*(reflection+24)/256);
        int distance=scn_displace(ray_length,hit.t);
        color=scn_mix(color,scn_active->horizon,scn_active->fog_distance>0?scn_clamp(distance/scn_active->fog_distance,0,100):0);
        layers[count]=color;reflect[count++]=bounce==bounces-1?0:reflection;
        if(reflection<8)break;
        scn_set(&eye,start.x,start.y,start.z);scn_set(&ray,reflected.x,reflected.y,reflected.z);
    }
    u32 result=layers[count-1];for(int i=count-2;i>=0;i--)result=scn_mix(layers[i],result,reflect[i]);return result;
}
static int scn_draw_rows(u32 *pixels,int width,int height,int stride,int focal,int row_begin,int row_end)
{
    if(!pixels||width<1||height<1||width>1920||height>1080||stride<width||stride>8192||focal<1||focal>4096||row_begin<0||row_end<row_begin||row_end>height)return SCN_INVALID;
    if(scn_active->error)return scn_active->error;
    for(int y=row_begin;y<row_end;y++){
        u32 *out=pixels+y*stride;
        for(int x=0;x<width;x++){ScnVec ray;scn_camera_ray(&ray,x,y,width,height,focal);*out++=0xFF000000u|scn_trace(&scn_eye,&ray);}
    }
    return SCN_OK;
}
