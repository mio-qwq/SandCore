/* mio：真正读取输入世界，逐项提交全部命中字段和整个工作区。
 * 只做查询验证；不是低清游戏画面，也不以此宣称游戏达到60FPS。 */
#include "SCENE.inc"
#include "SCVOX.inc"
static u16 fast_work[4098],slow_work[4098];
static ScvGrid fast_grid,slow_grid;
static u32 *result;
static int used=8,errors;
static volatile int vox_probe[8];
static void snapshot(void){
    u16 *out=(u16 *)(result+used);
    for(int i=0;i<4098;i++)out[i]=fast_work[i];used+=2049;
}
static void record(ScvGrid *grid,ScnVec *eye,ScnVec *ray,int distance,int skip){
    ScvHit hit;u32 *words=(u32 *)&hit;for(int i=0;i<11;i++)words[i]=0x6C12A0BFu;
    result[used++]=(u32)scv_query(grid,eye,ray,distance,skip,&hit);
    for(int i=0;i<11;i++)result[used++]=words[i];
}
int main(void){
    int win=sc_open_rgb("SCVOX query guards / mio",408,196);if(win<0)return 1;
    sc_fill_rgb(win,0,0,1920,1080,SCN_SHADE);
    sc_text_rgb(win,16,16,"SCVOX / exact voxel queries",SCN_IVORY);
    sc_text_rgb(win,16,48,"Full hit fields / guards / lifecycle",SCN_GLASS);
    u32 stat[2];if(sc_stat("HOME/VOX.IN",stat)||stat[1]<32)return 2;
    u8 *input=sc_alloc(stat[1]);if(!input)return 3;
    if(sc_read("HOME/VOX.IN",input,(int)stat[1])!=(int)stat[1])return 4;
    char *magic="SVXI1MIO";for(int i=0;i<8;i++)if(input[i]!=(u8)magic[i])return 5;
    u32 *header=(u32 *)input;int scenarios=(int)header[3],total=(int)header[4];
    if(header[2]!=1||scenarios!=5||total!=5120)return 6;
    int bytes=32+total*96+scenarios*5*8196+64;
    result=sc_alloc((u32)bytes);if(!result)return 7;
    int at=32;
    for(int scene=0;scene<scenarios;scene++){
        int *meta=(int *)(input+at);int w=meta[0],h=meta[1],d=meta[2],count=meta[3],cases=meta[4],target=meta[5];
        at+=24;u8 *cells=input+at;at+=(count+3)&~3;
        int *queries=(int *)(input+at);at+=cases*32;
        for(int i=0;i<4098;i++){fast_work[i]=0xA66A;slow_work[i]=0xA66A;}
        int capacity=((w+7)/8)*((h+7)/8)*((d+7)/8);
        if(scv_bind(&fast_grid,cells,w,h,d,fast_work+1,capacity)||scv_rebuild(&fast_grid))errors++;
        if(scv_bind(&slow_grid,cells,w,h,d,slow_work+1,capacity))errors++;
        u32 prior[12];for(int i=0;i<12;i++)prior[i]=((u32 *)&fast_grid)[i];
        if(scv_bind(&fast_grid,cells,w,h,d,fast_work+1,capacity-1)!=SCV_INVALID)errors++;
        if(scv_bind(&fast_grid,cells,129,h,d,fast_work+1,4096)!=SCV_INVALID)errors++;
        for(int i=0;i<12;i++)if(prior[i]!=((u32 *)&fast_grid)[i])errors++;
        if(scv_write(&fast_grid,-1,1)!=SCV_INVALID||scv_write(&fast_grid,count,1)!=SCV_INVALID
           ||scv_write(&fast_grid,target,256)!=SCV_INVALID||scv_write(&fast_grid,target,-1)!=SCV_INVALID)errors++;
        for(int number=0;number<cases;number++){
            if(number==256){if(scv_write(&fast_grid,target,4))errors++;}
            if(number==512){if(scv_write(&fast_grid,target,16))errors++;}
            if(number==768){if(scv_write(&fast_grid,target,0))errors++;}
            if(number==900){scv_invalidate(&fast_grid);cells[target]=7;}
            if(number==950){if(scv_rebuild(&fast_grid))errors++;}
            if(number==0||number==256||number==512||number==768||number==950)snapshot();
            int *entry=queries+number*8;ScnVec eye,ray;
            scn_set(&eye,entry[0],entry[1],entry[2]);scn_set(&ray,entry[3],entry[4],entry[5]);
            record(&slow_grid,&eye,&ray,entry[6],entry[7]);
            record(&fast_grid,&eye,&ray,entry[6],entry[7]);
        }
        vox_probe[3]=scene+1;sc_yield();
    }
    if(at!=(int)stat[1]||used*4!=bytes-64)errors++;
    for(int i=0;i<16;i++)result[used+i]=0x534D494Fu;
    char *signature="SVXQ1MIO";for(int i=0;i<8;i++)((u8 *)result)[i]=(u8)signature[i];
    result[2]=1;result[3]=(u32)scenarios;result[4]=(u32)total;result[5]=(u32)errors;
    result[6]=stat[1];result[7]=(u32)(bytes-96);
    vox_probe[1]=sc_write("HOME/VOX.OUT",result,bytes);vox_probe[2]=errors;
    sc_text_rgb(win,16,82,errors?"FAIL / inspect output":"5120 rays / full guards / done",SCN_IVORY);
    vox_probe[0]=1;while(sc_key()!=27)sc_yield();
    sc_free(result);sc_free(input);return errors?1:0;
}
