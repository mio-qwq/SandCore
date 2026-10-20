/* Copyright (c) 2026 mio. 公共协议只传值；身份由内核提供。 */
#include "sb.h"
#include <stdarg.h>
#include <limits.h>

void sb_record(void *p, unsigned size) {
    memset(p, 0, size);
    ((uint32_t *)p)[0] = size;
    ((uint32_t *)p)[1] = 1;
}
void sb_log(const char *fmt, ...) {
    char b[2048];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof(b), fmt, a);
    va_end(a);
    fputs(b, stderr);
}
int sb_buffer_append(SbBuffer *b, const void *p, uint32_t n) {
    if (n > SB_RESOURCE_MAX - b->length)
        return -1;
    uint32_t need = b->length + n + 1;
    if (need > b->capacity) {
        uint32_t cap = b->capacity ? b->capacity : 4096;
        while (cap < need) {
            if (cap > (SB_RESOURCE_MAX + 1) / 2) {
                cap = SB_RESOURCE_MAX + 1;
                break;
            }
            cap *= 2;
        }
        void *v = realloc(b->data, cap);
        if (!v)
            return -1;
        b->data = v;
        b->capacity = cap;
    }
    if (n)
        memcpy(b->data + b->length, p, n);
    b->length += n;
    b->data[b->length] = 0;
    return 0;
}
void sb_buffer_reset(SbBuffer *b) {
    free(b->data);
    memset(b, 0, sizeof(*b));
}
uint32_t sb_ticks(void) {
    ScMonotonicClock c;
    sb_record(&c, sizeof(c));
    return ScSystemMonotonic(&c) < 0 ? 0 : c.ticks.lo;
}
int sb_send(ScIpcChannel c, uint32_t kind, uint32_t id, uint32_t epoch, uint32_t a, uint32_t b,
            uint32_t x, uint32_t d, const void *data, unsigned n) {
    if (n > SB_CHUNK)
        return SC32_E_RANGE;
    unsigned total = sizeof(SbWire) + n;
    unsigned char *buf = malloc(total);
    if (!buf)
        return SC32_E_MEMORY;
    SbWire h = {SB_MAGIC, 1, kind, id, epoch, 0, a, b, x, d};
    memcpy(buf, &h, sizeof(h));
    if (n)
        memcpy(buf + sizeof(h), data, n);
    int status = ScIpcSend(c, SC32_IPC_FRAME_DATA, buf, total);
    free(buf);
    return status;
}
int sb_receive(ScIpcChannel c, SbPacket *p, ScIpcPacket *meta) {
    sb_record(meta, sizeof(*meta));
    int status = ScIpcReceive(c, p, sizeof(*p), meta);
    if (status < 0)
        return status;
    if (meta->type == SC32_IPC_FRAME_EOF)
        return SC32_E_BROKEN_PIPE;
    if (meta->bytes < sizeof(SbWire) || p->h.magic != SB_MAGIC || p->h.version != 1)
        return SC32_E_INVALID;
    return (int)(meta->bytes - sizeof(SbWire));
}
int sb_wait(ScIpcChannel c, uint32_t flags, uint32_t ticks) {
    ScBrokerWaitDesc w;
    uint32_t e = 0;
    sb_record(&w, sizeof(w));
    w.handle = c;
    w.ready_flags = flags;
    w.event_mask = SC32_EVENT_INPUT | SC32_EVENT_NETWORK | SC32_EVENT_IMAGE | SC32_EVENT_JOB;
    w.timeout = ticks;
    return ScBrokerWait(&w, &e);
}
int sb_get_worker_arguments(char *endpoint, size_t n, char *role, size_t m) {
    char args[1024];
    uint32_t count = 0;
    if (ScProcessArguments(args, sizeof(args), &count) < 0)
        return -1;
    char *space = strchr(args, ' ');
    if (!space)
        return -1;
    *space++ = 0;
    if (strlen(args) >= n || strlen(space) >= m)
        return -1;
    strcpy(endpoint, args);
    strcpy(role, space);
    return 0;
}
ScIpcChannel sb_worker_connect(int role, char **unused) {
    (void)unused;
    char name[128], arg[32];
    if (sb_get_worker_arguments(name, sizeof(name), arg, sizeof(arg)))
        return 0;
    ScIdentityInfo identity;
    sb_record(&identity, sizeof(identity));
    if (ScIdentityQuery(&identity) < 0)
        return 0;
    char *colon = strchr(arg, ':');
    if (!colon)
        return 0;
    *colon++ = 0;
    uint32_t pid = strtoul(arg, 0, 10), generation = strtoul(colon, 0, 10);
    if (!pid || !generation)
        return 0;
    ScIpcConnectDesc desc;
    ScIpcChannel c = 0;
    sb_record(&desc, sizeof(desc));
    desc.name = name;
    desc.namespace_id = SC32_IPC_NAMESPACE_SESSION;
    desc.session = identity.session;
    uint32_t began = sb_ticks();
    while (ScIpcConnect(&desc, &c) < 0) {
        if (sb_ticks() - began > 500)
            return 0;
        ScProcessSleep(1);
    }
    ScBrokerIdentity peer;
    int status;
    do {
        sb_record(&peer, sizeof(peer));
        status = ScIpcPeer(c, &peer);
        if (status == SC32_E_AGAIN)
            sb_wait(c, SC32_BROKER_READY_WRITE, 1);
    } while (status == SC32_E_AGAIN && sb_ticks() - began < 500);
    if (status < 0 || peer.pid != pid || peer.generation != generation ||
        peer.session != identity.session || peer.uid != identity.uid ||
        peer.realm != identity.realm) {
        ScIpcClose(c);
        return 0;
    }
    while (sb_send(c, SB_HELLO, 0, 0, role, 0, 0, 0, 0, 0) == SC32_E_AGAIN)
        sb_wait(c, SC32_BROKER_READY_WRITE, 10);
    return c;
}
void sb_base_path(char *out, size_t n) {
    char p[128];
    /* GETUSER kind3 为当前可执行文件路径，旧 ABI 原样使用。 */
    extern int sb_raw(int, int, int, int, int, int, int);
    if (sb_raw(0x70, 3, 0, (int)p, sizeof(p), 0, 0) < 0) {
        if (n)
            out[0] = 0;
        return;
    }
    char *slash = strrchr(p, '/');
    if (slash)
        *slash = 0;
    if (strlen(p) >= n) {
        out[0] = 0;
        return;
    }
    strcpy(out, p);
}
int sb_file_read(const char *path, SbBuffer *b) {
    ScFile f;
    ScFileOpenDesc d;
    sb_record(&d, sizeof(d));
    d.path = path;
    d.flags = SC32_FILE_READ;
    int s = ScFileOpen(&d, &f);
    if (s < 0)
        return s;
    unsigned char buf[16384];
    for (;;) {
        ScIoResult r;
        sb_record(&r, sizeof(r));
        s = ScFileRead(f, buf, sizeof(buf), &r);
        if (s == SC32_PENDING) {
            ScProcessSleep(1);
            continue;
        }
        if (s < 0)
            break;
        if (!r.transferred)
            break;
        if (sb_buffer_append(b, buf, r.transferred)) {
            s = SC32_E_MEMORY;
            break;
        }
    }
    ScFileClose(f);
    return s;
}
int sb_file_write(const char *path, const void *p, unsigned n) {
    /* 小配置使用整体替换；SCL/大文档经真实文件对象分块写入，避开 64 KiB Path ABI 上限。 */
    if (n <= SC32_PATH_IO_MAX) {
        uint32_t wrote = 0;
        int status = ScPathReplace(path, p, n, &wrote);
        return status < 0 ? status : wrote == n ? 0 : SC32_E_IO;
    }
    ScFile file = 0;
    ScFileOpenDesc desc;
    sb_record(&desc, sizeof(desc));
    desc.path = path;
    desc.flags = SC32_FILE_WRITE | SC32_FILE_CREATE | SC32_FILE_TRUNCATE;
    desc.create_mode = 3;
    int status = ScFileOpen(&desc, &file);
    if (status < 0)
        return status;
    unsigned at = 0;
    while (at < n) {
        unsigned take = n - at;
        if (take > 65536)
            take = 65536;
        ScIoResult result;
        sb_record(&result, sizeof(result));
        status = ScFileWrite(file, (const unsigned char *)p + at, take, &result);
        if (result.transferred > take) {
            status = SC32_E_IO;
            break;
        }
        at += result.transferred;
        if (status == SC32_E_AGAIN) {
            status = SC32_OK;
            ScProcessSleep(1);
            continue;
        }
        if (status < 0)
            break;
        if (!result.transferred) {
            status = SC32_E_IO;
            break;
        }
    }
    if (status >= 0)
        status = ScFileFlush(file);
    int closed = ScFileClose(file);
    return status < 0 ? status : closed;
}
