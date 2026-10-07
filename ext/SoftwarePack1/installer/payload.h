/* 包内 SPLZ1MIO 资源合同；平台 SCX/SCB2 不变。 */
#ifndef PAYLOAD_H
#define PAYLOAD_H
#include "SCAPI.H"
typedef struct {
 const char *name,*label,*cfg_name;
 const u8 *scx;u32 scx_size,scx_packed_size,scx_crc;
 const u8 *icon;u32 icon_size,icon_packed_size,icon_crc;
 const char *cfg,*desc;
} exapp_entry;
extern const exapp_entry EXAPP[];
extern const int EXAPP_COUNT;
#endif
