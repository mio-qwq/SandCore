/* mio：Lens/Canvas共用的私有位图契约，公共入口仍在SCAPI.H。
 * SCB头用逐字节小端解析，不能把外来BMP长度/负高度当成指针。
 * palette槽0在图片中为黑色，在桌面图标中为透明，两者不混淆。 */
static inline u32 image_u32(const u8 *p){
    return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24);
}
static inline int image_u16(const u8 *p){
    return p[0]|(p[1]<<8);
}
static inline void image_put32(u8 *p,u32 v){
    for(int i=0;i<4;i++){
        p[i]=(u8)v;
        v>>=8;

    }
}
static inline void image_header(u8 *out,int width,int height)
{
    const char *magic="SCB1MIO";
    for(int i=0;i<8;i++)out[i]=(u8)magic[i];
    image_put32(out+8,width);
    image_put32(out+12,height);
    image_put32(out+16,1);
    image_put32(out+20,0x004F494D);
}
static inline int image_scb(const u8 *p)
{
    return p[0]=='S'&&p[1]=='C'&&p[2]=='B'&&p[3]=='1'&&p[4]=='M'&&p[5]=='I'&&p[6]=='O'&&!p[7]
    &&image_u32(p+16)==1&&image_u32(p+20)==0x004F494D;
}
