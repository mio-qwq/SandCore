/* SPLZ1MIO 严格解压；先查每一 token 的输入、回引用和输出边界。
 * 最大原长来自构建期清单，坏包不允许写入目标文件。 */
static int ins_unpack(const u8 *source,u32 length,u8 *out,u32 size)
{
    const char *magic="SPLZ1MIO";if(length<12||!out)return -1;
    for(int i=0;i<8;i++)if(source[i]!=(u8)magic[i])return -1;
    u32 declared=(u32)source[8]|((u32)source[9]<<8)|((u32)source[10]<<16)|((u32)source[11]<<24);
    if(declared!=size)return -1;
    u32 at=12,used=0;
    while(used<size){
        if(at>=length)return -1;u8 flags=source[at++];
        for(int bit=0;bit<8&&used<size;bit++){
            if(flags&(1<<bit)){if(at>=length)return -1;out[used++]=source[at++];}
            else{
                if(length-at<2)return -1;
                u32 token=source[at]|((u32)source[at+1]<<8);at+=2;
                u32 distance=(token>>4)+1,count=(token&15)+3;
                if(distance>used||count>size-used)return -1;
                while(count--){out[used]=out[used-distance];used++;}
            }
        }
    }return at==length?0:-1;
}
static u8 *ins_icons[INS_MAX];
static void ins_load_icons(void)
{
    for(int i=0;i<EXAPP_COUNT&&i<INS_MAX;i++){
        const exapp_entry *a=&EXAPP[i];u8 *raw=sc_alloc(a->icon_size);if(!raw)continue;
        int w,h;
        if(ins_unpack(a->icon,a->icon_packed_size,raw,a->icon_size)||ins_crc32(raw,a->icon_size)!=a->icon_crc||ex_icon_header(raw,a->icon_size,&w,&h)){sc_free(raw);continue;}
        ins_icons[i]=raw;
    }
}
