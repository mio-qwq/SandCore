/* mio：配置全量解析到候选表，成功才替换。用户把文件写了一半、字段
 * 超长或包含未知格式时，已经可用的菜单不能消失；返回错误给设置。
 * 图标以16×16槽位副本缓存，彩色SCB与旧SCF同存，字体仍由独立系统
 * 字库提供。绝不将图标点阵作为替代汉字字体。 */
#include "desktop.h"
#include "fs.h"
#include "gfx.h"
#include "palette.h"
static desk_item_t menus[32],icons[24],candidate;
static int nmenus,nicons;
static char text[8192];
static u8 image[24+32*32];
static int upper(int c){
    return c>='a'&&c<='z'?c-'a'+'A':c;
}
static int prefix(const char *s,const char *p)
{
    while(*p)if(upper(*s++)!=upper(*p++))return 0;
    return 1;
}
static void icon_load(desk_item_t *item,const char *path)
{

    /* 没有图标仍显示统一文件符号，命令与图标故障分开：装一个程序
     * 不应因可选图片缺失就失去开始菜单入口。所有槽位先清零透明。 */
    for(int i=0;i<256;i++)item->pixels[i]=0;
    int n=fs_read(path,image,sizeof(image));
    if(n>=288 && prefix((char *)image,"SCF1MIO") && image[7]==0 && *(u32 *)(image+8)==1) {

        for(int y=0;y<16;y++)for(int x=0;x<16;x++)
        if(image[16+y*17+x]=='#')item->pixels[y*16+x]=PAL_TITLE;
        return;

    }
    if(n>=24 && prefix((char *)image,"SCB1MIO") && image[7]==0 && *(u32 *)(image+16)==1
    && *(u32 *)(image+20)==0x004F494Du) {

        u32 w=*(u32 *)(image+8),h=*(u32 *)(image+12);
        u32 info[2];
        if(w && h && w<=32 && h<=32 && n==(int)(24+w*h) && !fs_stat(path,info) && info[1]==(u32)n) {

            for(int y=0;y<16;y++)for(int x=0;x<16;x++)item->pixels[y*16+x]=image[24+(y*h/16)*w+x*w/16];
            return;

        }

    }
    for(int y=2;y<14;y++)for(int x=3;x<13;x++)
    item->pixels[y*16+x]=(x==3||x==12||y==2||y==13)?PAL_UI_CYAN+6:PAL_UI_PANEL;
}
static int line(char **source,char *out,int capacity)
{

    int n=0;
    while(**source && **source!='\n') {

        char c=*(*source)++;
        if(c=='\r')continue;
        if(n+1>=capacity)return -1;
        out[n++]=c;

    }
    if(**source=='\n')(*source)++;
    out[n]=0;
    return n;
}
static int menu_parse(void)
{

    int n=fs_read("SYS/MENU.CFG",text,sizeof(text)-1);
    if(n<9)return -1;
    text[n]=0;
    char *source=text,head[16];
    if(line(&source,head,sizeof(head))!=9 || !prefix(head,"SMENU1MIO"))return -1;
    int count=0;
    /* 两遍解析复用一条候选记录：第一遍只验证，第二遍才替换。
     * 不在低端 BSS 同时放32条重复图标，给字库/调试保留空间。 */
    for(int pass=0;pass<2;pass++) {

        source=text;
        line(&source,head,sizeof(head));
        count=0;
        while(*source) {

            char record[256];
            int bytes=line(&source,record,sizeof(record));
            if(bytes<0)return -1;
            if(!bytes || record[0]=='#')continue;
            if(count==32)return -1;
            desk_item_t *item=&candidate;
            item->link[0]=0;
            char path[64];
            int field=0,at=0;
            for(int i=0;i<=bytes;i++) {

                char c=record[i];
                if(c=='|' || !c) {

                    if(field==0)item->label[at]=0;
                    else if(field==1)item->command[at]=0;
                    else if(field==2)path[at]=0;
                    else return -1;
                    field++;
                    at=0;
                    if(!c)break;
                    continue;

                }
                if((u8)c<32)return -1;
                int max=field==0?31:field==1?127:63;
                if(field>2 || at>=max)return -1;
                if(field==0)item->label[at++]=c;
                else if(field==1)item->command[at++]=c;
                else path[at++]=c;

            }
            if(field!=3 || !item->label[0] || !item->command[0])return -1;
            if(pass){
                icon_load(item,path);
                item->used=1;
                menus[count]=*item;

            }
            count++;

        }

    }
    nmenus=count;
    return 0;
}
static void icons_load(void)
{

    nicons=0;
    for(int i=0;i<fs_count() && nicons<24;i++) {

        const char *name=fs_name(i);
        int len=0;
        while(name[len])len++;
        if(len<9 || !prefix(name,"DESK/") || !prefix(name+len-4,".LNK"))continue;
        int n=fs_read(name,text,511);
        if(n<=0)continue;
        text[n]=0;
        char *source=text,path[64];
        desk_item_t *item=&icons[nicons];
        if(line(&source,item->label,32)<1 || line(&source,item->command,128)<1 || line(&source,path,64)<0)continue;
        int k=0;
        do{
            item->link[k]=name[k];

        }
        while(name[k++]);
        icon_load(item,path);
        item->used=1;
        nicons++;

    }
}
int desktop_reload(void){
    int result=menu_parse();
    icons_load();
    return result;
}
int desktop_count(int menu){
    return menu?nmenus:nicons;
}
const desk_item_t *desktop_item(int menu,int index)
{
    int n=menu?nmenus:nicons;
    return index>=0 && index<n?&(menu?menus:icons)[index]:0;
}
void desktop_icon(int x,int y,int size,const desk_item_t *item)
{

    if(!item || size<1)return;
    for(int row=0;row<size;row++)for(int col=0;col<size;col++) {

        u8 c=item->pixels[(row*16/size)*16+col*16/size];
        if(c)gfx_pset(x+col,y+row,c);

    }
}
