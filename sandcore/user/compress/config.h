#ifndef SAND_MINIZ_CONFIG_H
#define SAND_MINIZ_CONFIG_H
#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#define MINIZ_NO_MALLOC
#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_ZLIB_APIS
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#define MINIZ_LITTLE_ENDIAN 1
#define MINIZ_USE_UNALIGNED_LOADS_AND_STORES 1
#define MINIZ_HAS_64BIT_REGISTERS 0
#define TDEFL_LESS_MEMORY 1
#include "../SCAPI.H"
/* 只给上游核心提供字节操作，不发布标准C名字或链接宿主libc。 */
static void *sand_mz_copy(void *out,const void *in,unsigned int n)
{u8 *a=out;const u8 *b=in;for(unsigned int i=0;i<n;i++)a[i]=b[i];return out;}
static void *sand_mz_move(void *out,const void *in,unsigned int n)
{u8 *a=out;const u8 *b=in;if(a>b){while(n){n--;a[n]=b[n];}}else for(unsigned int i=0;i<n;i++)a[i]=b[i];return out;}
static void *sand_mz_set(void *out,int c,unsigned int n)
{u8 *a=out;for(unsigned int i=0;i<n;i++)a[i]=(u8)c;return out;}
#define memcpy sand_mz_copy
#define memmove sand_mz_move
#define memset sand_mz_set
#endif
