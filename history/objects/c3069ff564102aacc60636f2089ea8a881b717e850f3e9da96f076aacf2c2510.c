/* mio：普通三环射线夹具；结果不是内核API，也不是游戏存档。
 * 同一像素布局和光照顺序可比较太阳缓存失效、上下文切回及垂直相机。
 * 观察数组仅便于只读定位，完成条件还要真实IDE文件和画布逐字节相等。 */
#include "SCAPI.H"
#include "NEWSCENE.inc"
#define PROBE_W 96
#define PROBE_H 64
#define PROBE_PAIRS 178
#define PROBE_HEAD (12+PROBE_PAIRS*2)
static int probe_pairs[PROBE_PAIRS][2]={{0,0},
{0,1},
{0,-1},
{0,257},
{0,-513},
{0,2147483647},
{0,(-2147483647-1)},
{1,0},
{1,1},
{1,-1},
{1,257},
{1,-513},
{1,2147483647},
{1,(-2147483647-1)},
{-1,0},
{-1,1},
{-1,-1},
{-1,257},
{-1,-513},
{-1,2147483647},
{-1,(-2147483647-1)},
{32767,0},
{32767,1},
{32767,-1},
{32767,257},
{32767,-513},
{32767,2147483647},
{32767,(-2147483647-1)},
{-32767,0},
{-32767,1},
{-32767,-1},
{-32767,257},
{-32767,-513},
{-32767,2147483647},
{-32767,(-2147483647-1)},
{32768,0},
{32768,1},
{32768,-1},
{32768,257},
{32768,-513},
{32768,2147483647},
{32768,(-2147483647-1)},
{-32768,0},
{-32768,1},
{-32768,-1},
{-32768,257},
{-32768,-513},
{-32768,2147483647},
{-32768,(-2147483647-1)},
{8388607,0},
{8388607,1},
{8388607,-1},
{8388607,257},
{8388607,-513},
{8388607,2147483647},
{8388607,(-2147483647-1)},
{-8388607,0},
{-8388607,1},
{-8388607,-1},
{-8388607,257},
{-8388607,-513},
{-8388607,2147483647},
{-8388607,(-2147483647-1)},
{8388608,0},
{8388608,1},
{8388608,-1},
{8388608,257},
{8388608,-513},
{8388608,2147483647},
{8388608,(-2147483647-1)},
{-8388608,0},
{-8388608,1},
{-8388608,-1},
{-8388608,257},
{-8388608,-513},
{-8388608,2147483647},
{-8388608,(-2147483647-1)},
{2147483647,0},
{2147483647,1},
{2147483647,-1},
{2147483647,257},
{2147483647,-513},
{2147483647,2147483647},
{2147483647,(-2147483647-1)},
{-2147483647,0},
{-2147483647,1},
{-2147483647,-1},
{-2147483647,257},
{-2147483647,-513},
{-2147483647,2147483647},
{-2147483647,(-2147483647-1)},
{(-2147483647-1),0},
{(-2147483647-1),1},
{(-2147483647-1),-1},
{(-2147483647-1),257},
{(-2147483647-1),-513},
{(-2147483647-1),2147483647},
{(-2147483647-1),(-2147483647-1)},
{-1040328355,745819051},
{-1185536826,1329831991},
{32611245,793188962},
{-460268046,-1211727509},
{-603743879,-2087489173},
{-5102792,-1321778341},
{-1405387337,-1409754777},
{-1984499987,1757435792},
{2035138927,-187042204},
{-1460156749,309044157},
{-1568797716,-784722645},
{-1396598661,-2045635657},
{-1541401319,-347086291},
{2114368884,-857369981},
{-768535208,-2137712616},
{-1545446719,409752319},
{946134391,788446601},
{-642329750,1419107156},
{1397424713,1986455426},
{-972263391,1353200310},
{1428787457,-1424507651},
{962355448,-686773561},
{1845100381,-1896665442},
{959505656,355226977},
{-473200210,782554333},
{178441498,-241863664},
{-870143489,-1933649197},
{260439563,958638433},
{1222727613,1878625089},
{249656250,-1969069192},
{1703759287,547913311},
{1414089418,-1931928370},
{-1047624114,374823623},
{2071969643,-435578560},
{423630309,-932021656},
{1077249526,-46048816},
{-855441307,-1923409464},
{-579208276,1467664311},
{1664322595,-1448745746},
{-1455421224,1680256285},
{608212535,-1197471471},
{-2061199617,-1403373265},
{1445573324,-1746840871},
{964633947,732597065},
{107693121,-1552875813},
{-849888378,362604820},
{1854089216,-1766348826},
{-948607390,-654299604},
{2106324928,395193069},
{-863946504,856700678},
{1761361742,-950072205},
{493070707,1038403103},
{-1587479284,-1415109376},
{-715357809,-1755218458},
{-2139115372,1872899549},
{-750493639,-1089057714},
{-925443648,2083396739},
{-400165961,-1821852063},
{612903093,-1467033674},
{703309577,114727235},
{1415996249,890294360},
{1747020682,1800840774},
{2093124537,-1390390577},
{57529801,-1038385495},
{-342697258,-1274090705},
{-1214065097,255324735},
{2031339706,-9216214},
{1964904264,998631624},
{-1023467817,-1613139055},
{743354990,-1715741749},
{877956925,1365272474},
{1328932592,-305396477},
{774642063,841942916},
{-256223826,455602003},
{1943656536,1247937328},
{-58439417,-2035747128},
{481215636,36558770},
{-193422727,-1895464388},
{2110974830,1722978418},
{1485271692,-1855959174}};
static u32 probe_blob[PROBE_HEAD+6*PROBE_W*PROBE_H+16];
static ScnScene scene_a,scene_b;
static ScnObject objects_a[16],objects_b[16];
static volatile int scene_probe[16];
#ifdef SCN_HAS_PREPARED_PRIMARY
static ScnPrepared prepared[17];
static int check_queries(void)
{
    int errors=0;
    for(int i=0;i<160;i++){
        ScnVec eye,ray;eye=scn_eye;
        if(i&1){eye.x+=37;eye.y-=19;eye.z+=11;}
        scn_set(&ray,(i*71)%1024-512,(i*43)%600-300,256+(i*29)%512);
        ScnHit cached,plain;int enabled=scn_prepared_valid;
        scn_intersect(&eye,&ray,&cached,2000000);
        if(scn_occluded(&eye,&ray,2000000)!=cached.found)errors++;
        scn_prepared_valid=0;scn_intersect(&eye,&ray,&plain,2000000);
        scn_prepared_valid=enabled;
        if(cached.found!=plain.found||cached.index!=plain.index||cached.t!=plain.t)errors++;
        if(cached.found&&(cached.p.x!=plain.p.x||cached.p.y!=plain.p.y||cached.p.z!=plain.p.z
            ||cached.n.x!=plain.n.x||cached.n.y!=plain.n.y||cached.n.z!=plain.n.z))errors++;
    }
    return errors;
}
#endif
static void prepare(ScnScene *scene,ScnObject *objects)
{
    scn_bind(scene,objects,16);
    scn_sphere(-160,150,350,140,SCN_METAL,190,1);
    scn_box(100,0,320,290,270,530,SCN_OAK,50,2);
    ScnVec a,b,c;
    scn_set(&a,-440,0,620);scn_set(&b,360,0,650);scn_set(&c,-60,430,660);
    scn_triangle(&a,&b,&c,SCN_PAINT,72);
    scn_camera(0,230,-500,0,120,410);
}
int main(void)
{
    int win=sc_open_rgb("SCENE / mio",310,170),info[5];
    if(win<0||sc_info(win,info))return 1;
    int width=info[2],height=info[3];
    u32 *frame=(u32 *)sc_alloc((u32)(width*height*4));
    if(!frame)return 2;
    for(int i=0;i<width*height;i++)frame[i]=0xFF000000u|SCN_SHADE;
    sc_frame32(win,frame,(u32)(width*height*4));
    scene_probe[0]=1;
    u8 *magic=(u8 *)probe_blob;char *signature="SRAY1MIO";
    for(int i=0;i<8;i++)magic[i]=(u8)signature[i];
    probe_blob[2]=1;probe_blob[3]=PROBE_W;probe_blob[4]=PROBE_H;
    probe_blob[5]=6;probe_blob[6]=PROBE_PAIRS;
    for(int i=0;i<PROBE_PAIRS;i++){
        probe_blob[12+2*i]=(u32)scn_fraction_ratio(probe_pairs[i][0],probe_pairs[i][1],8);
        probe_blob[13+2*i]=(u32)scn_fraction_ratio(probe_pairs[i][0],probe_pairs[i][1],16);
    }
    for(int i=0;i<16;i++)probe_blob[PROBE_HEAD+6*PROBE_W*PROBE_H+i]=0x534D494Fu;
    prepare(&scene_a,objects_a);prepare(&scene_b,objects_b);
    scene_b.floor_color=SCN_SAND;scene_b.sky_top=SCN_SEA;
    scn_set(&scene_b.sun,0,0,0);
    for(int stage=0;stage<6;stage++){
        scn_use(stage==2?&scene_b:&scene_a);
        if(stage==1)scn_set(&scene_a.sun,45,100,-130);
        if(stage==3)scn_set(&scene_a.sun,50000,-16384,32768);
        if(stage==4)scn_camera(0,650,350,0,0,350);
        if(stage==5){scn_camera(0,230,-500,0,120,410);scn_set(&scene_a.sun,-115,205,102);}
#ifdef SCN_HAS_PREPARED_PRIMARY
        u8 *guard=(u8 *)&prepared[16];
        for(int i=0;i<(int)sizeof(ScnPrepared);i++)guard[i]=0xA5;
        if(scn_prepare_primary(&scn_eye,prepared,16))return 5;
        probe_blob[11]+=(u32)check_queries();
        for(int i=0;i<(int)sizeof(ScnPrepared);i++)if(guard[i]!=0xA5)probe_blob[11]++;
#endif
        int begin=sc_tick();
        u32 *pixels=probe_blob+PROBE_HEAD+stage*PROBE_W*PROBE_H;
        // 两个相邻半开批次组合，检验分行时不会留下接缝或漏最后一行。
        if(scn_draw_rows(pixels,PROBE_W,PROBE_H,PROBE_W,74,0,31)
            ||scn_draw_rows(pixels,PROBE_W,PROBE_H,PROBE_W,74,31,PROBE_H))return 3;
        scene_probe[4+stage]=sc_tick()-begin;
        int left=(stage%3)*PROBE_W,top=(stage/3)*PROBE_H;
        for(int y=0;y<PROBE_H;y++)for(int x=0;x<PROBE_W;x++)
            if(left+x<width&&top+y<height)frame[(top+y)*width+left+x]=pixels[y*PROBE_W+x];
        if(sc_frame32(win,frame,(u32)(width*height*4)))return 4;
        scene_probe[1]=stage+1;sc_yield();
    }
    probe_blob[7]=(u32)scn_draw_rows(probe_blob,PROBE_W,PROBE_H,PROBE_W,74,-1,0);
    probe_blob[8]=(u32)scn_draw_rows(probe_blob,PROBE_W,PROBE_H,PROBE_W,74,0,PROBE_H+1);
    probe_blob[9]=(u32)scn_bind(&scene_b,objects_b,0);
    probe_blob[10]=(u32)scn_use(0);
#ifdef SCN_HAS_PREPARED_PRIMARY
    if(scn_prepare_primary(&scn_eye,prepared,1)!=SCN_INVALID||scn_prepared_valid)probe_blob[11]++;
    scn_prepare_primary(&scn_eye,prepared,16);
    if(scn_sphere(500,100,600,40,SCN_SAND,8,0)<0||scn_prepared_valid)probe_blob[11]++;
    probe_blob[11]+=(u32)check_queries();
    scn_prepare_primary(&scn_eye,prepared,16);scn_use(&scene_b);
    probe_blob[11]+=(u32)check_queries();
    scn_reset();if(scn_prepared_valid)probe_blob[11]++;
#endif
    scene_probe[2]=sc_write("HOME/SRAY-NEW.BIN",probe_blob,(int)sizeof(probe_blob));
    scene_probe[3]=(int)frame;scene_probe[10]=width;scene_probe[11]=height;
    scene_probe[0]=2;
    for(;;){if(sc_key()==27)break;sc_yield();}
    sc_free(frame);return 0;
}
