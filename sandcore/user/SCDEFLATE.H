#ifndef SCDEFLATE_H
#define SCDEFLATE_H
#include "SCIO.H"
/* 缓冲读者保留解码器未消费的尾部，GZIP CRC/下一成员不会丢失。
 * bounded表示压缩源有明确长度（ZIP）；GZIP可以读到流EOF。 */
typedef struct {int fd,at,used,eof,error,bounded;u32 remaining;u8 bytes[4096];} sand_reader;
void sand_reader_init(sand_reader *,int,u32,int);
int sand_reader_fill(sand_reader *);
int sand_reader_byte(sand_reader *);
int sand_reader_exact(sand_reader *,void *,u32);
u32 sand_crc(u32,const void *,u32);
int sand_deflate(int,int,int,u32 *,u32 *,u32 *);
int sand_inflate(sand_reader *,int,u32,u32 *,u32 *);
static u32 sand_little(const u8 *p){return (u32)p[0]|(u32)p[1]<<8|(u32)p[2]<<16|(u32)p[3]<<24;}
static void sand_putlittle(u8 *p,u32 n){p[0]=(u8)n;p[1]=(u8)(n>>8);p[2]=(u8)(n>>16);p[3]=(u8)(n>>24);}
static u32 sand_output_limit(void)
{char text[256];int n;return sc_user(4,"DECOMPRESS_LIMIT",text,256)>=0 && !cli_integer(text,&n) && n>=0?(u32)n:16777216u;}
#endif
