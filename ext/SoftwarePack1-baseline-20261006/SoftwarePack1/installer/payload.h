/* 自动生成：tools/mkpayload.py。载荷内嵌接口，勿手改。 */
#ifndef PAYLOAD_H
#define PAYLOAD_H
#include "SCAPI.H"

typedef struct {
    const char *name;
    const char *label;
    const char *cfg_name;
    const u8 *scx;
    const u32 scx_size;
    const u32 scx_crc;
    const u8 *icon;
    const u32 icon_size;
    const char *cfg;
    const char *desc;
} exapp_entry;
extern const exapp_entry EXAPP[];
extern const int EXAPP_COUNT;
#endif
