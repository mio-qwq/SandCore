#ifndef SANDCORE_SCOFMT_H
#define SANDCORE_SCOFMT_H
/* 磁盘对象使用字节偏移；禁止把宿主结构体填充或指针宽度写入格式。 */
#define SCO_MAGIC "SCO1MIO"
#define SCO_VERSION 1u
#define SCO_HEADER 96u
#define SCO_MACHINE 3u
#define SCO_MAX_FILE 16777216u
#define SCO_MAX_MEMORY 67108864u
#define SCO_SECTION_BYTES 24u
#define SCO_SYMBOL_BYTES 32u
#define SCO_RELOC_BYTES 24u
#define SCO_FILE_BYTES 16u
#define SCO_LINE_BYTES 16u
#define SCO_TEXT 1u
#define SCO_RODATA 2u
#define SCO_DATA 3u
#define SCO_BSS 4u
#define SCO_LOCAL 0u
#define SCO_GLOBAL 1u
#define SCO_FUNCTION 1u
#define SCO_OBJECT 2u
#define SCO_ABS32 1u
#define SCO_PC32 2u
#define SCO_TARGET_SYMBOL 0u
#define SCO_TARGET_SECTION 1u
#endif
