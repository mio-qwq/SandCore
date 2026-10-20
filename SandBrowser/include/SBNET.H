/* Copyright (c) 2026 mio. 网络库公开 C ABI，socket/TLS 资源归 context。 */
#ifndef SANDBROWSER_NET_H
#define SANDBROWSER_NET_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct SbNetContext SbNetContext;
typedef int (*SbNetCancel)(void *);
typedef struct {
    uint32_t size, version;
    const char *url, *method, *content_type;
    const void *body;
    uint32_t body_bytes, timeout_ticks;
    SbNetCancel cancelled;
    void *context;
} SbNetRequest;
typedef struct {
    uint32_t size, version, status, secure;
    char url[2048], content_type[256];
    unsigned char *body;
    uint32_t body_bytes;
    int error, tls_error;
    char message[256];
} SbNetResult;
SbNetContext *SbNetCreate(void);
int SbNetFetch(SbNetContext *, const SbNetRequest *, SbNetResult *);
void SbNetRelease(SbNetResult *);
void SbNetDestroy(SbNetContext *);
#ifdef __cplusplus
}
#endif
#endif
