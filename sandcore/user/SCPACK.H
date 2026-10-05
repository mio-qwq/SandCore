#ifndef SCPACK_H
#define SCPACK_H
#include "SCIO.H"
/* 压缩核心的项目私有接口：整数运算、有界流和SandFS事务。 */
#define SC_PACK_LIMIT 16777216u
int sand_pack_bzip(int input,int output,int decode,int level,int small,u32 limit);
int sand_pack_lzma(int input,int output,u32 limit);
void *sand_pack_malloc(unsigned int bytes);
void sand_pack_free(void *address);
void *sand_pack_copy(void *to,const void *from,unsigned int bytes);
void *sand_pack_fill(void *to,int value,unsigned int bytes);
#endif
