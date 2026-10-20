/* Copyright (c) 2026 mio. SandBrowser 的值类型协议与平台边界。 */
#ifndef SANDBROWSER_H
#define SANDBROWSER_H
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif
#include "SC32API.H"
#include "SC32GUI.H"
#include "SC32POPUP.H"
#include "SC32BROKER.H"
#include "SC32FONT.H"
#include "SC32IMAGE.H"

#define SB_VERSION "0.1.1"
#define SB_MAGIC 0x31574253u
#define SB_URL 2048
#define SB_CHUNK 48000
#define SB_RESOURCE_MAX (16u * 1024u * 1024u)
#define SB_SURFACE_BYTES (1920u * 1080u * 4u)
enum { SB_ROLE_PAGE = 1, SB_ROLE_NET = 2 };
enum {
    SB_HELLO = 1,
    SB_NAVIGATE,
    SB_FETCH,
    SB_HEADERS,
    SB_DATA,
    SB_DONE,
    SB_ERROR,
    SB_VIEW,
    SB_POINTER,
    SB_KEY,
    SB_FRAME,
    SB_FRAME_ACK,
    SB_TITLE,
    SB_STATUS,
    SB_LINK,
    SB_EDIT,
    SB_EDIT_RESULT,
    SB_ALERT,
    SB_FIND,
    SB_SOURCE,
    SB_SAVE,
    SB_SHUTDOWN,
    SB_CANCEL,
    SB_SURFACE,
    SB_SCRIPT_OPTION,
    SB_FORM_POST
};
/* 线上的消息不包含指针。payload 紧接 40B 头，精确长度逐次检查。 */
typedef struct {
    uint32_t magic, version, kind, id, epoch, flags, a, b, c, d;
} SbWire;
typedef struct {
    SbWire h;
    unsigned char body[SB_CHUNK];
} SbPacket;
typedef struct {
    unsigned char *data;
    uint32_t length, capacity;
} SbBuffer;
typedef struct {
    char scheme[8], host[256], path[SB_URL], origin[320];
    uint32_t port;
    int secure;
} SbUrl;
typedef struct {
    uint32_t *pixels;
    int width, height, stride, x0, y0, x1, y1;
} SbCanvas;
typedef struct {
    int pixels, bold, italic, underline;
} SbFont;
typedef struct {
    uint32_t generation, width, height, document_width, document_height;
    int scroll_x, scroll_y;
    uint32_t reserved[8];
} SbSurface;

void sb_record(void *p, unsigned size);
void sb_log(const char *format, ...);
int sb_buffer_append(SbBuffer *, const void *, uint32_t);
void sb_buffer_reset(SbBuffer *);
int sb_url_parse(const char *, SbUrl *, char *, size_t);
int sb_url_resolve(const char *, const char *, char *, size_t);
int sb_same_origin(const char *, const char *);
void sb_percent_encode(const char *, char *, size_t, int);
uint32_t sb_ticks(void);
int sb_random(void *, unsigned);
int sb_utc(uint32_t *, uint32_t *);
int sb_send(ScIpcChannel, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
            const void *, unsigned);
int sb_receive(ScIpcChannel, SbPacket *, ScIpcPacket *);
int sb_wait(ScIpcChannel, uint32_t, uint32_t);
ScIpcChannel sb_worker_connect(int, char **);
int sb_get_worker_arguments(char *, size_t, char *, size_t);
void sb_base_path(char *, size_t);
int sb_file_read(const char *, SbBuffer *);
int sb_file_write(const char *, const void *, unsigned);
void sb_fill(SbCanvas *, int, int, int, int, uint32_t);
void sb_line(SbCanvas *, int, int, int, int, uint32_t);
void sb_bevel(SbCanvas *, int, int, int, int, int);
void sb_text(SbCanvas *, int, int, const char *, SbFont, uint32_t);
int sb_text_width(const char *, SbFont);
void sb_blit(SbCanvas *, int, int, int, int, const uint32_t *, int, int, int);
void sb_circle(SbCanvas *, int, int, int, uint32_t);
void sb_init_fonts(void);
ScStatus sb_window_present(ScWindow, const uint32_t *, unsigned, unsigned);
int SbBrowserMain(void);
int SbPageMain(void);
int SbNetMain(void);
int SbRuntimeInit(void);
void SbRuntimeFini(void);
#ifdef __cplusplus
}
#endif
#endif
