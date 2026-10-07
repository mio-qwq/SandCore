#include "SCIO.H"
/* 全映射字体探针：字符/字形编号由宿主从原TTF独立解析，实际位图
 * 全部来自客体公开ABI。正文流式写独立文件，宿主再与FreeType比较；
 * 不把遍历计数或自洽摘要当作栅格正确性证据。 */
static u32 bitmap[SC_FONT_BITMAP_WORDS+2],info[18];
static u8 output_buffer[4096];
static u32 output_used,output_bytes;
static int output_fd;
static int flush_output(void)
{
    if(!output_used)return 0;
    if(cli_write(output_fd,output_buffer,output_used)!=(int)output_used)return -1;
    output_bytes+=output_used;output_used=0;return 0;
}
static int emit(const void *data,u32 bytes)
{
    const u8 *input=data;
    for(u32 at=0;at<bytes;){u32 room=sizeof(output_buffer)-output_used,take=bytes-at;
        if(take>room)take=room;for(u32 i=0;i<take;i++)output_buffer[output_used++]=input[at++];
        if(output_used==sizeof(output_buffer) && flush_output()<0)return -1;}
    return 0;
}
static int unchanged(void)
{for(int i=0;i<SC_FONT_BITMAP_WORDS+2;i++)if(bitmap[i]!=0xA10F0001)return 0;return 1;}
static int reject(u32 scalar,u32 face,u32 pixels,u32 words)
{
    for(int i=0;i<SC_FONT_BITMAP_WORDS+2;i++)bitmap[i]=0xA10F0001;
    return sc_glyph_bitmap(scalar,face,pixels,bitmap+1,words)==-1 && unchanged();
}
static int boundaries(u32 face)
{
    if(!reject(0xD800,face,face,400) || !reject(0xDFFF,face,face,400)
        || !reject(0x110000,face,face,400) || !reject(0x41,13,face,400)
        || !reject(0x41,face,0,400) || !reject(0x41,face,65,400)
        || !reject(0x41,face,face,399) || !reject(0x41,face,face,4097))return -1;
    if(sc_glyph_bitmap(0x41,face,face,(u32 *)0xFFFFFFFCu,400)!=-1)return -1;
    info[0]=0xA10F0002;info[17]=0xA10F0003;
    if(sc_fontinfo2(face,info+1)<0 || info[0]!=0xA10F0002 || info[17]!=0xA10F0003)return -1;
    return info[1]==1 && info[3]==face && info[4]==1?0:-1;
}
int main(void)
{
    if(cli_parse()!=3)return 2;int face_value;
    if(cli_integer(cli_argv[0],&face_value)<0 || (face_value!=12 && face_value!=16))return 2;
    u32 face=(u32)face_value,stat[2];
    if(sc_stat(cli_argv[1],stat)<0 || stat[0]!=1 || !stat[1] || stat[1]%8 || stat[1]>1048576)return 3;
    u32 *mapping=sc_alloc(stat[1]);if(!mapping)return 4;
    if(sc_read(cli_argv[1],mapping,stat[1])!=(int)stat[1]){sc_free(mapping);return 5;}
    u32 count=stat[1]/8;
    if(boundaries(face)<0 || info[6]!=count){sc_free(mapping);return 6;}
    /* 预留的是流式磁盘事务上界，成功提交释放实际未用扇区；没有
     * 为整份7540字输出分配等量用户RAM。完整400字尾必须全为零。 */
    if(count>(0x7FFFFFFFu-80u)/1608u){sc_free(mapping);return 7;}
    output_fd=sc_stream_open(cli_argv[2],2,count*1608u+80u);if(output_fd<0){sc_free(mapping);return 8;}
    u32 header[4]={0x31464D53,1,face,count};int failure=0;
    if(emit(header,sizeof(header))<0 || emit(info+1,64)<0)failure=9;
    u32 began=(u32)sc_tick(),previous=0;
    for(u32 index=0;index<count && !failure;index++){
        u32 scalar=mapping[index*2],expected=mapping[index*2+1];
        if((index && scalar<=previous) || !expected){failure=10;break;}previous=scalar;
        bitmap[0]=0xA10F0004;bitmap[SC_FONT_BITMAP_WORDS+1]=0xA10F0005;
        int advance=sc_glyph_bitmap(scalar,face,face,bitmap+1,SC_FONT_BITMAP_WORDS);
        u32 *glyph=bitmap+1,width=glyph[6],height=glyph[7],words=(width+31)/32;
        if(advance<=0 || bitmap[0]!=0xA10F0004 || bitmap[SC_FONT_BITMAP_WORDS+1]!=0xA10F0005
            || glyph[0]!=1 || glyph[1]!=info[2] || glyph[2]!=expected || glyph[3]!=1
            || glyph[4]!=face || glyph[5]!=(u32)advance || glyph[10]!=face
            || width>128 || !height || height>96 || glyph[13] || glyph[14] || glyph[15]){failure=11;break;}
        for(u32 row=0;row<96 && !failure;row++)for(u32 word=0;word<4;word++){
            u32 value=glyph[16+row*4+word];
            if((row>=height || word>=words) && value){failure=12;break;}
            if(row<height && word+1==words && (width&31) && (value&((1u<<(32-(width&31)))-1))){failure=12;break;}
        }
        u32 prefix[2]={scalar,(u32)advance};
        if(!failure && (emit(prefix,8)<0 || emit(glyph,64)<0))failure=13;
        for(u32 row=0;row<height && !failure;row++)if(emit(glyph+16+row*4,words*4)<0)failure=13;
    }
    if(!failure && flush_output()<0)failure=14;
    int closed=sc_stream_close(output_fd,failure?0:1);sc_free(mapping);
    if(closed<0 && !failure)failure=15;
    if(failure)return failure;
    cli_text(1,"PASS every original Unicode mapping returned real glyph and native bitmap\n");
    cli_text(1,"PASS bitmap/info boundaries and zero unused rows columns\n");
    cli_text(1,"mapped=");cli_number(1,(int)count);cli_text(1,"\nfont_ticks=");cli_number(1,(int)((u32)sc_tick()-began));
    cli_text(1,"\nbitmap_bytes=");cli_number(1,(int)output_bytes);cli_text(1,"\n");return 0;
}
