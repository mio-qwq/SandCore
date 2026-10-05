/* mio：只读取夹具输入，在真正用户态算几何并写盘。
 * 输出每射线两个完整现场，分别是准备起点与强制不同准备起点；
 * 只把失败计数放头部不足以发现误命中，所以九字段和遮挡/错误
 * 一起保存。未命中预填哨兵，确认库没有乱写未定义点/法线。 */
#include "SCAPI.H"
#include "SCENE.inc"
static ScnScene test_scene,other_scene;
static ScnObject objects[4],others[4];
static struct {u32 head[4];ScnExact data[4];u32 tail[4];} workspace;
static u8 saved_workspace[sizeof(workspace)];
static volatile int exact_probe[8];
static u32 *output;
static int used=8;
static u32 lifecycle_errors;
static void require(int condition,int bit){if(!condition)lifecycle_errors|=1u<<bit;}
static void fill_workspace(void){for(int i=0;i<(int)sizeof(workspace);i++)((u8 *)&workspace)[i]=0xA5;}
static int same_workspace(void){
    for(int i=0;i<(int)sizeof(workspace);i++)if(((u8 *)&workspace)[i]!=saved_workspace[i])return 0;
    return 1;
}
static int guard(void){
    for(int i=0;i<4;i++)if(workspace.head[i]!=0xA5A5A5A5u||workspace.tail[i]!=0xA5A5A5A5u)return 0;
    for(int i=0;i<(int)sizeof(ScnExact);i++)if(((u8 *)&workspace.data[3])[i]!=0xA5)return 0;
    return 1;
}
static void reset_face(void){
    ScnVec a,b,c;scn_bind(&test_scene,objects,4);test_scene.floor=0;
    scn_set(&a,-256,-256,256);scn_set(&b,256,-256,256);scn_set(&c,0,256,256);
    scn_triangle_exact(&a,&b,&c,SCN_GLASS,166);
}
static void lifecycle(void){
    ScnVec origin,ray,a,b,c;ScnHit hit;scn_set(&origin,0,0,0);scn_set(&ray,0,0,256);
    require(sizeof(ScnExact)==92&&sizeof(ScnPrepared)==64&&sizeof(ScnHit)==36&&sizeof(ScnObject)==56&&sizeof(ScnScene)==120,0);
    reset_face();fill_workspace();require(scn_prepare_exact(&origin,workspace.data,4)==0&&scn_exact_valid,1);
    require(scn_triangle_exact(0,&origin,&origin,SCN_GLASS,0)==-1&&test_scene.count==1,2);
    require(scn_triangle_exact(&origin,&origin,&origin,SCN_GLASS,0)==-1&&test_scene.count==1,3);
    scn_set(&a,8193,0,0);require(scn_triangle_exact(&a,&origin,&ray,SCN_GLASS,0)==-1&&test_scene.count==1,4);
    test_scene.count=4;require(scn_triangle_exact(&objects[0].low,&objects[0].high,&objects[0].third,SCN_GLASS,0)==-2&&test_scene.count==4,5);
    test_scene.count=1;
    require(scn_prepare_exact(0,workspace.data,4)==-1&&!scn_exact_valid,6);
    require(scn_prepare_exact(&origin,0,4)==-1&&!scn_exact_valid,7);
    require(scn_prepare_exact(&origin,workspace.data,0)==-1&&!scn_exact_valid,8);
    test_scene.count=3;require(scn_prepare_exact(&origin,workspace.data,2)==-1&&!scn_exact_valid,9);test_scene.count=1;
    require(scn_prepare_exact(&origin,workspace.data,4097)==-1&&!scn_exact_valid,10);
    scn_set(&a,-8193,0,0);require(scn_prepare_exact(&a,workspace.data,4)==-1&&!scn_exact_valid,11);
    // 第二个坏面必须在写第一个面之前被发现，失败保持全部工作区字节。
    objects[1]=objects[0];test_scene.count=2;fill_workspace();
    for(int i=0;i<(int)sizeof(workspace);i++)saved_workspace[i]=((u8 *)&workspace)[i];
    objects[1].third.x=8193;require(scn_prepare_exact(&origin,workspace.data,4)==-1&&same_workspace(),12);
    objects[1]=objects[0];objects[1].third=objects[1].low;
    require(scn_prepare_exact(&origin,workspace.data,4)==-1&&same_workspace()&&!scn_exact_valid,13);
    require(guard(),14);test_scene.count=1;test_scene.error=0;
    require(scn_prepare_exact(&origin,workspace.data,4)==0,15);
    a=origin;a.x=65536;scn_intersect(&a,&ray,&hit,2000000);
    require(!hit.found&&test_scene.error==-1,16);test_scene.error=0;
    // use不会重置另一上下文；缓存有场景身份，切回仍能使用原批次。
    other_scene=test_scene;other_scene.objects=others;other_scene.count=0;other_scene.floor=0;
    scn_use(&other_scene);scn_intersect(&origin,&ray,&hit,2000000);require(!hit.found,17);
    scn_use(&test_scene);scn_intersect(&origin,&ray,&hit,2000000);require(hit.found&&hit.t==65536,18);
    for(int i=0;i<1;i++){objects[i].low.z+=256;objects[i].high.z+=256;objects[i].third.z+=256;}
    require(scn_prepare_exact(&origin,workspace.data,4)==0,19);
    scn_intersect(&origin,&ray,&hit,2000000);require(hit.found&&hit.t==131072,19);
    scn_disable_exact();require(!scn_exact_valid&&!scn_exact,20);
    scn_prepare_exact(&origin,workspace.data,4);scn_box(-1,-1,-1,1,1,1,SCN_METAL,0,0);require(!scn_exact_valid,21);
    scn_prepare_exact(&origin,workspace.data,4);scn_reset();require(!scn_exact_valid&&!test_scene.count,22);
    scn_prepare_exact(&origin,workspace.data,4);scn_bind(&other_scene,others,4);require(!scn_exact_valid,23);
    reset_face();scn_set(&a,0,0,256);scn_set(&b,1,0,256);scn_set(&c,0,1,256);
    scn_reset();test_scene.floor=0;require(scn_triangle_exact(&a,&b,&c,SCN_GLASS,0)==0,24);
    scn_prepare_exact(&origin,workspace.data,4);scn_intersect(&origin,&ray,&hit,65537);
    require(hit.found&&hit.t==65536,24);scn_intersect(&origin,&ray,&hit,65536);require(!hit.found,25);
    // 4096是公开上界，不能只验证小数组就宣称容量全范围正确。
    // 物体和工作区都是普通用户堆；两端各32B护栏，完成后先解除
    // 批次引用再释放，最后恢复仍存活的静态上下文，避免悬挂指针。
    u8 *object_memory=sc_alloc(4096u*56u+64u),*exact_memory=sc_alloc(4096u*92u+64u);
    require(object_memory&&exact_memory,26);
    if(object_memory&&exact_memory){
        for(int i=0;i<32;i++){
            object_memory[i]=object_memory[32+4096*56+i]=0xA5;
            exact_memory[i]=exact_memory[32+4096*92+i]=0xA5;
        }
        ScnScene large_scene;ScnObject *large_objects=(ScnObject *)(object_memory+32);
        ScnExact *large_exact=(ScnExact *)(exact_memory+32);
        require(scn_bind(&large_scene,large_objects,4096)==0,27);large_scene.floor=0;
        for(int i=0;i<4096;i++)require(scn_triangle_exact(&a,&b,&c,SCN_GLASS,0)==i,27);
        require(scn_triangle_exact(&a,&b,&c,SCN_GLASS,0)==-2&&large_scene.count==4096,27);
        large_scene.error=0;require(scn_prepare_exact(&origin,large_exact,4096)==0,28);
        scn_intersect(&origin,&ray,&hit,65537);
        require(hit.found&&hit.index==0&&hit.t==65536&&hit.p.z==256&&hit.n.z==-256,28);
        for(int i=0;i<32;i++)require(object_memory[i]==0xA5&&object_memory[32+4096*56+i]==0xA5
            &&exact_memory[i]==0xA5&&exact_memory[32+4096*92+i]==0xA5,29);
    }
    scn_disable_exact();if(object_memory)sc_free(object_memory);if(exact_memory)sc_free(exact_memory);
    scn_bind(&test_scene,objects,4);
}
static void query(ScnVec *origin,ScnVec *ray,int limit){
    ScnHit hit;hit.p.x=hit.p.y=hit.p.z=0x13579BDF;hit.n=hit.p;
    test_scene.error=0;scn_intersect(origin,ray,&hit,limit);
    output[used++]=(u32)hit.found;output[used++]=(u32)hit.index;output[used++]=(u32)hit.t;
    output[used++]=(u32)hit.p.x;output[used++]=(u32)hit.p.y;output[used++]=(u32)hit.p.z;
    output[used++]=(u32)hit.n.x;output[used++]=(u32)hit.n.y;output[used++]=(u32)hit.n.z;
    output[used++]=(u32)scn_occluded(origin,ray,limit);output[used++]=(u32)test_scene.error;
}
int main(void){
    int win=sc_open("Exact triangles / mio",310,160);if(win<0)return 1;
    page(win,"Exact geometry","Planes / edges / lifecycle");
    u32 stat[2];if(sc_stat("HOME/EXACT.IN",stat)||stat[1]!=32+1024*148)return 2;
    u8 *input=sc_alloc(stat[1]);if(!input)return 3;
    if(sc_read("HOME/EXACT.IN",input,(int)stat[1])!=(int)stat[1])return 4;
    char *magic="EXIN1MIO";for(int i=0;i<8;i++)if(input[i]!=(u8)magic[i])return 5;
    u32 *head=(u32 *)input;if(head[2]!=1||head[3]!=1024||head[4]!=37||head[5]!=22)return 6;
    int bytes=32+1024*88+64;output=sc_alloc((u32)bytes);if(!output)return 7;
    lifecycle();int errors=0;
    for(int i=0;i<1024;i++){
        int *row=(int *)(input+32)+i*37;ScnVec origin,ray,prep;
        scn_bind(&test_scene,objects,4);test_scene.floor=0;
        for(int j=0;j<3;j++)if(scn_triangle_exact((ScnVec *)(row+j*9),(ScnVec *)(row+j*9+3),(ScnVec *)(row+j*9+6),SCN_GLASS,166)!=j)errors++;
        scn_set(&origin,row[27],row[28],row[29]);scn_set(&ray,row[30],row[31],row[32]);
        scn_set(&prep,row[34],row[35],row[36]);fill_workspace();
        if(scn_prepare_exact(&prep,workspace.data,4))errors++;query(&origin,&ray,row[33]);
        if(!guard())errors++;
        // 改为另一起点准备，强制验证二次射线没有误用主起点平面。
        prep.x=prep.x==8192?prep.x-1:prep.x+1;
        if(scn_prepare_exact(&prep,workspace.data,4))errors++;query(&origin,&ray,row[33]);
        if(!guard())errors++;
        if((i&63)==0)sc_yield();
    }
    for(int i=0;i<16;i++)output[used+i]=0x534D494Fu;
    magic="EXOT1MIO";for(int i=0;i<8;i++)((u8 *)output)[i]=(u8)magic[i];
    output[2]=1;output[3]=1024;output[4]=22;
#if defined(__SCCC_WIDE__)
    output[5]=1;
#else
    output[5]=0;
#endif
    output[6]=lifecycle_errors;output[7]=1024*88;
    exact_probe[4]=(int)output[5];exact_probe[2]=errors;
    exact_probe[1]=sc_write("HOME/EXACT.OUT",output,bytes);
    page(win,errors||lifecycle_errors?"FAIL / exact geometry":"1024 complete ray pairs","Esc: return");
    exact_probe[0]=1;while(sc_key()!=27)sc_yield();
    scn_disable_exact();sc_free(output);sc_free(input);return errors||lifecycle_errors;
}
