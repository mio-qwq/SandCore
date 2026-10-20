/* Copyright (c) 2026 mio. HTTP/TLS 库及独立的网络进程。 */
#include "sb.h"
#include "SBNET.H"
#include "sb_transport.h"
#include "bearssl.h"
#include "miniz_tinfl.h"
#include <ctype.h>
#include <strings.h>
#ifndef SB_TRUST_HEADER
#define SB_TRUST_HEADER "anchors.h"
#endif
#include SB_TRUST_HEADER
typedef struct {
    char host[256], path[1024], name[128], value[2048];
    int secure;
    uint32_t order;
} Cookie;
struct SbNetContext {
    int fd, secure, cancelled, tls_ready, io_error;
    uint32_t began, timeout;
    const SbNetRequest *request;
    br_ssl_client_context client;
    br_x509_minimal_context x509;
    br_sslio_context io;
    unsigned char tls[BR_SSL_BUFSIZE_BIDI];
    Cookie cookies[64];
    uint32_t serial;
    uint64_t clock_unix, clock_tick;
    uint32_t clock_hz;
    int network_clock, clock_retried;
};
static int stopped(SbNetContext *c) {
    return c->cancelled || sb_ticks() - c->began > c->timeout ||
           (c->request->cancelled && c->request->cancelled(c->request->context));
}
static void pause_net(void) {
    uint32_t ev;
    ScSystemWait(SC32_EVENT_NETWORK, 1, &ev);
}
static int raw_read(void *v, unsigned char *b, size_t n) {
    SbNetContext *c = v;
    for (;;) {
        if (stopped(c))
            return -1;
        int s = sb_socket_read(c->fd, b, n, 0);
        if (s == -11) {
            pause_net();
            continue;
        }
        if (s < 0)
            c->io_error = s;
        return s;
    }
}
static int raw_write(void *v, const unsigned char *b, size_t n) {
    SbNetContext *c = v;
    for (;;) {
        if (stopped(c))
            return -1;
        int s = sb_socket_write(c->fd, b, n, 0);
        if (s == -11) {
            pause_net();
            continue;
        }
        if (s <= 0)
            c->io_error = s ? s : -32;
        return s > 0 ? s : -1;
    }
}
static int tls_read(void *v, unsigned char *b, size_t n) {
    int s = raw_read(v, b, n);
    if (s == 0)
        ((SbNetContext *)v)->io_error = -32;
    return s > 0 ? s : -1;
}
static int stream_read(SbNetContext *c, void *p, unsigned n) {
    return c->secure ? br_sslio_read(&c->io, p, n) : raw_read(c, p, n);
}
static int stream_write(SbNetContext *c, const void *p, unsigned n) {
    unsigned at = 0;
    while (at < n) {
        int s = c->secure ? br_sslio_write(&c->io, (const char *)p + at, n - at)
                          : raw_write(c, (const unsigned char *)p + at, n - at);
        if (s <= 0)
            return -1;
        at += s;
    }
    return c->secure ? br_sslio_flush(&c->io) : 0;
}
static int resolve(SbNetContext *c, const char *host, uint32_t *out, unsigned budget) {
    unsigned part = 0, ip = 0;
    const char *p = host;
    int valid = 1;
    for (int i = 0; i < 4; i++) {
        part = 0;
        unsigned digits = 0;
        while (isdigit((unsigned char)*p)) {
            part = part * 10 + *p++ - '0';
            digits++;
        }
        if (!digits || digits > 3 || part > 255) {
            valid = 0;
            break;
        }
        ip = (ip << 8) | part;
        if (i < 3) {
            if (*p++ != '.') {
                valid = 0;
                break;
            }
        } else if (*p)
            valid = 0;
    }
    if (valid) {
        *out = ip;
        return 0;
    }
    if (!strcmp(host, "localhost")) {
        *out = 0x7f000001;
        return 0;
    }
    int ticket = sb_dns_begin(host);
    if (ticket < 0)
        return ticket;
    int status = -110;
    uint32_t began = sb_ticks();
    while (!stopped(c) && (!budget || sb_ticks() - began < budget)) {
        uint32_t a[8] = {0};
        status = sb_dns_poll(ticket, a);
        if (status < 0)
            break;
        if (a[1] == 2) {
            *out = a[2];
            status = 0;
            break;
        }
        if (a[1] == 3) {
            status = a[3] ? (int)a[3] : -110;
            break;
        }
        pause_net();
        status = -110;
    }
    sb_dns_close(ticket);
    return stopped(c) ? -115 : status;
}
static uint32_t ntp_u32(const unsigned char *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static int ntp_reply(const unsigned char *p, unsigned n, const unsigned char nonce[8],
                     uint64_t *unix_seconds) {
    if (n < 48 || n > 512 || (p[0] >> 6) == 3 || (p[0] & 7) != 4 ||
        ((p[0] >> 3) & 7) < 3 || ((p[0] >> 3) & 7) > 4 ||
        p[1] == 0 || p[1] > 15 || memcmp(p + 24, nonce, 8))
        return -1;
    uint64_t timestamp = ntp_u32(p + 40);
    if (!timestamp && !ntp_u32(p + 44))
        return -1;
    /* Resolve the 2036 era wrap into the supported 2020..2099 interval. */
    if (timestamp < 2208988800u)
        timestamp += 0x100000000ull;
    timestamp -= 2208988800u;
    if (timestamp < 1577836800ull || timestamp >= 4102444800ull)
        return -1;
    *unix_seconds = timestamp;
    return 0;
}
static int network_time_sample(SbNetContext *c, const char *server, unsigned hz,
                                uint32_t *address, uint64_t *seconds) {
    if (resolve(c, server, address, 2 * hz) < 0)
        return -1;
    unsigned char request[48] = {0}, reply[512];
    request[0] = (4u << 3) | 3u;
    if (sb_random(request + 40, 8))
        return -1;
    int fd = sb_socket_open(2);
    if (fd < 0)
        return -1;
    uint32_t destination[4] = {1, *address, 123, 0};
    uint32_t began = sb_ticks();
    int result = -1, sent;
    do {
        sent = sb_socket_write(fd, request, sizeof(request), destination);
        if (sent == -11)
            pause_net();
    } while (sent == -11 && !stopped(c) && sb_ticks() - began < 2 * hz);
    if (sent == sizeof(request))
        while (!stopped(c) && sb_ticks() - began < 2 * hz) {
            uint32_t source[4] = {0};
            int n = sb_socket_read(fd, reply, sizeof(reply), source);
            if (n == -11) {
                pause_net();
                continue;
            }
            if (n < 0)
                break;
            if (source[0] == 1 && source[1] == *address && source[2] == 123 &&
                ntp_reply(reply, n, request + 40, seconds) == 0) {
                result = 0;
                break;
            }
        }
    sb_socket_close(fd);
    return result;
}
static int clock_snapshot(ScMonotonicClock *clock, uint64_t *ticks) {
    sb_record(clock, sizeof(*clock));
    if (ScSystemMonotonic(clock) != SC32_OK || !clock->hz || clock->hz > 1000000)
        return -1;
    *ticks = ((uint64_t)clock->ticks.hi << 32) | clock->ticks.lo;
    return 0;
}
static int synchronize_time(SbNetContext *c) {
    static const char *servers[] = {"ntp.aliyun.com", "time.windows.com", "time.cloudflare.com"};
    struct {uint32_t address; uint64_t seconds, tick;} samples[3];
    unsigned count = 0;
    ScMonotonicClock clock;
    uint64_t ticks;
    if (clock_snapshot(&clock, &ticks))
        return -1;
    unsigned hz = clock.hz;
    for (unsigned i = 0; i < 3 && !stopped(c); i++) {
        uint32_t address;
        uint64_t seconds;
        if (network_time_sample(c, servers[i], hz, &address, &seconds) < 0 ||
            clock_snapshot(&clock, &ticks))
            continue;
        if (clock.hz != hz)
            return -1;
        for (unsigned j = 0; j < count; j++) {
            if (samples[j].address == address || ticks < samples[j].tick)
                continue;
            uint64_t previous = samples[j].seconds + (ticks - samples[j].tick) / hz;
            uint64_t difference = seconds > previous ? seconds - previous : previous - seconds;
            if (difference <= 5) {
                c->clock_unix = seconds;
                c->clock_tick = ticks;
                c->clock_hz = hz;
                c->network_clock = 1;
                return 0;
            }
        }
        samples[count].address = address;
        samples[count].seconds = seconds;
        samples[count++].tick = ticks;
    }
    return -1;
}
static int certificate_time(SbNetContext *c, uint32_t *days, uint32_t *seconds) {
    if (!c->network_clock && sb_utc(days, seconds) == 0)
        return 0;
    ScMonotonicClock clock;
    uint64_t ticks;
    if (clock_snapshot(&clock, &ticks))
        return -1;
    if (!c->network_clock || clock.hz != c->clock_hz || ticks < c->clock_tick ||
        (ticks - c->clock_tick) / clock.hz >= 3600) {
        if (synchronize_time(c) || clock_snapshot(&clock, &ticks))
            return -1;
    }
    if (clock.hz != c->clock_hz || ticks < c->clock_tick)
        return -1;
    uint64_t utc = c->clock_unix + (ticks - c->clock_tick) / c->clock_hz;
    *days = 719528u + (uint32_t)(utc / 86400u);
    *seconds = (uint32_t)(utc % 86400u);
    return 0;
}
static void disconnect(SbNetContext *c) {
    if (c->fd >= 0)
        sb_socket_close(c->fd);
    c->fd = -1;
}
static const char *tls_error_text(int error) {
    switch (error) {
    case BR_ERR_NO_RANDOM:
        return "Secure random generator was not seeded";
    case BR_ERR_UNSUPPORTED_VERSION:
    case BR_ERR_BAD_VERSION:
        return "The server and this TLS 1.2 client could not agree on a TLS version";
    case BR_ERR_BAD_CIPHER_SUITE:
    case BR_ERR_INVALID_ALGORITHM:
        return "The server requested an unsupported TLS algorithm";
    case BR_ERR_BAD_SIGNATURE:
    case BR_ERR_X509_BAD_SIGNATURE:
        return "Certificate or handshake signature verification failed";
    case BR_ERR_X509_EXPIRED:
    case BR_ERR_X509_TIME_UNKNOWN:
        return "Certificate date validation failed; network time could not correct it";
    case BR_ERR_X509_BAD_SERVER_NAME:
        return "The certificate does not match the website hostname";
    case BR_ERR_X509_NOT_TRUSTED:
        return "The certificate issuer is not in the installed trust store";
    case BR_ERR_X509_UNSUPPORTED:
        return "The certificate uses an unsupported format or algorithm";
    case BR_ERR_IO:
        return "The TLS connection was interrupted or closed before completion";
    default:
        if (error >= BR_ERR_RECV_FATAL_ALERT && error < BR_ERR_SEND_FATAL_ALERT)
            return "The server rejected the TLS handshake";
        if (error >= BR_ERR_SEND_FATAL_ALERT)
            return "TLS negotiation failed";
        return "TLS protocol or certificate validation failed";
    }
}
static int fail(SbNetContext *c, SbNetResult *r, int code, const char *message) {
    r->error = code;
    r->tls_error = c->secure && c->tls_ready ? br_ssl_engine_last_error(&c->client.eng) : 0;
    snprintf(r->message, sizeof(r->message), "%s%s%s%s", message,
             r->tls_error ? ": " : "", r->tls_error ? tls_error_text(r->tls_error) : "",
             stopped(c) ? " (cancelled or timed out)" : "");
    if (r->tls_error == BR_ERR_IO && c->io_error) {
        size_t n = strlen(r->message);
        snprintf(r->message + n, sizeof(r->message) - n, " (socket %d)", c->io_error);
    }
    disconnect(c);
    return code;
}
static int connect_to(SbNetContext *c, SbUrl *u, SbNetResult *r) {
    c->tls_ready = 0;
    c->io_error = 0;
    c->secure = u->secure;
    uint32_t days = 0, sec = 0;
    if (c->secure) {
        if (!sb_random_sources())
            return fail(c, r, -4,
                        "Hardware RNG unavailable. Enable RDRAND in the virtual CPU; use the "
                        "SandBrowser RUN-HTTPS.BAT launcher, or add +rdrand to QEMU -cpu");
        /* Obtain time before connecting: a slow time lookup must not leave the
         * HTTPS server waiting on an idle TCP connection before ClientHello. */
        if (certificate_time(c, &days, &sec))
            return fail(c, r, -5, "Cannot obtain a valid UTC time. Allow DNS and UDP port 123 "
                                 "to time servers, then retry; or set the system clock");
    }
    uint32_t ip;
    if (resolve(c, u->host, &ip, 0) < 0)
        return fail(c, r, -1, "DNS lookup failed");
    c->fd = sb_socket_open(1);
    if (c->fd < 0)
        return fail(c, r, -2, "Cannot open TCP socket");
    uint32_t a[4] = {1, ip, u->port, 0};
    int s = sb_socket_connect(c->fd, a);
    while (s == -115 && !stopped(c)) {
        uint32_t state[16] = {0};
        s = sb_socket_status(c->fd, state);
        if (s < 0)
            break;
        if (state[4]) {
            s = (int)state[4];
            break;
        }
        if (state[3] & 2) {
            s = 0;
            break;
        }
        s = -115;
        pause_net();
    }
    if (s < 0 || stopped(c))
        return fail(c, r, -3, "TCP connection failed");
    if (c->secure) {
        unsigned char seed[32];
        if (sb_random(seed, sizeof(seed)))
            return fail(c, r, -4, "Hardware RNG is temporarily unavailable; retry the page");
        br_ssl_client_init_full(&c->client, &c->x509, sb_anchors,
                                sizeof(sb_anchors) / sizeof(sb_anchors[0]));
        c->tls_ready = 1;
        br_ssl_engine_set_versions(&c->client.eng, BR_TLS12, BR_TLS12);
        static const char *protocols[] = {"http/1.1"};
        br_ssl_engine_set_protocol_names(&c->client.eng, protocols, 1);
        br_x509_minimal_set_time(&c->x509, days, sec);
        br_ssl_engine_set_buffer(&c->client.eng, c->tls, sizeof(c->tls), 1);
        br_ssl_engine_inject_entropy(&c->client.eng, seed, sizeof(seed));
        memset(seed, 0, sizeof(seed));
        if (!br_ssl_client_reset(&c->client, u->host, 0))
            return fail(c, r, -6, "Cannot initialize TLS");
        br_sslio_init(&c->io, &c->client.eng, tls_read, c, raw_write, c);
    }
    return 0;
}
static char *trim(char *p) {
    while (*p == ' ' || *p == '\t')
        p++;
    char *e = p + strlen(p);
    while (e > p && (e[-1] == ' ' || e[-1] == '\t'))
        *--e = 0;
    return p;
}
static int token_safe(const char *p) {
    for (; *p; p++)
        if ((unsigned char)*p < 32 || *p == 127)
            return 0;
    return 1;
}
static void set_cookie(SbNetContext *c, SbUrl *u, char *text) {
    char *semi = strchr(text, ';');
    if (semi)
        *semi++ = 0;
    char *eq = strchr(text, '=');
    if (!eq)
        return;
    *eq++ = 0;
    char *name = trim(text);
    char *value = trim(eq);
    if (!*name || strlen(name) >= 128 || strlen(value) >= 2048 || !token_safe(name) ||
        !token_safe(value))
        return;
    Cookie v = {0};
    strcpy(v.host, u->host);
    strcpy(v.name, name);
    strcpy(v.value, value);
    strcpy(v.path, "/");
    char *slash = strrchr(u->path, '/');
    if (slash && slash != u->path) {
        size_t n = slash - u->path + 1;
        if (n < sizeof(v.path)) {
            memcpy(v.path, u->path, n);
            v.path[n] = 0;
        }
    }
    int deletion = 0;
    while (semi) {
        char *next = strchr(semi, ';');
        if (next)
            *next++ = 0;
        char *p = trim(semi);
        char *equal = strchr(p, '=');
        if (equal)
            *equal++ = 0;
        if (!strcasecmp(p, "Secure"))
            v.secure = 1;
        else if (!strcasecmp(p, "Path") && equal && *equal == '/' && strlen(equal) < sizeof(v.path))
            strcpy(v.path, equal);
        else if (!strcasecmp(p, "Domain") && equal) {
            while (*equal == '.')
                equal++; /* conservative host-only policy */
            if (strcasecmp(equal, u->host))
                return;
        } else if (!strcasecmp(p, "Max-Age") && equal && strtol(equal, 0, 10) <= 0)
            deletion = 1;
        semi = next;
    }
    if (v.secure && !u->secure)
        return;
    unsigned slot = 0;
    uint32_t oldest = ~0u;
    for (unsigned i = 0; i < 64; i++) {
        Cookie *t = c->cookies + i;
        if (!strcmp(t->host, v.host) && !strcmp(t->path, v.path) && !strcmp(t->name, v.name)) {
            slot = i;
            break;
        }
        if (t->order < oldest) {
            oldest = t->order;
            slot = i;
        }
    }
    if (deletion)
        memset(c->cookies + slot, 0, sizeof(Cookie));
    else {
        v.order = ++c->serial;
        c->cookies[slot] = v;
    }
}
static int line_read(SbNetContext *c, char *b, unsigned cap) {
    unsigned n = 0;
    while (n + 1 < cap) {
        char x;
        int s = stream_read(c, &x, 1);
        if (s <= 0)
            return -1;
        if (x == '\n') {
            if (n && b[n - 1] == '\r')
                n--;
            b[n] = 0;
            return n;
        }
        if (x == 0)
            return -1;
        b[n++] = x;
    }
    return -1;
}
static int exact(SbNetContext *c, SbBuffer *b, uint32_t count) {
    if (count > SB_RESOURCE_MAX - b->length)
        return -1;
    unsigned char data[16384];
    while (count) {
        unsigned take = count < sizeof(data) ? count : sizeof(data);
        int n = stream_read(c, data, take);
        if (n <= 0 || sb_buffer_append(b, data, n))
            return -1;
        count -= n;
    }
    return 0;
}
static uint32_t crc32(const void *v, unsigned n) {
    uint32_t crc = ~0u;
    const unsigned char *p = v;
    while (n--) {
        crc ^= *p++;
        for (int i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (0xedb88320u & -(int)(crc & 1));
    }
    return ~crc;
}
static uint32_t little32(const unsigned char *p) {
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static int inflate_stream(const unsigned char *data, unsigned bytes, SbBuffer *out,
                          unsigned flags, unsigned *consumed) {
    tinfl_decompressor state;
    tinfl_init(&state);
    unsigned at = 0, begin = out->length;
    if (sb_buffer_append(out, "", 0))
        return -1;
    for (;;) {
        if (out->length + 1 >= out->capacity) {
            unsigned capacity = out->capacity * 2;
            if (capacity > SB_RESOURCE_MAX + 2) capacity = SB_RESOURCE_MAX + 2;
            if (capacity <= out->capacity) return -1;
            void *next = realloc(out->data, capacity);
            if (!next) return -1;
            out->data = next; out->capacity = capacity;
        }
        size_t input = bytes - at, output = out->capacity - out->length - 1;
        tinfl_status status = tinfl_decompress(&state, data + at, &input, out->data + begin,
                                               out->data + out->length, &output,
                                               flags | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
        at += input; out->length += output;
        if (out->length > SB_RESOURCE_MAX) return -1;
        out->data[out->length] = 0;
        if (status == TINFL_STATUS_DONE) { *consumed = at; return 0; }
        if (status != TINFL_STATUS_HAS_MORE_OUTPUT || (!input && !output)) return -1;
    }
}
static int decompress_body(SbBuffer *b, int gzip) {
    SbBuffer out = {0};
    unsigned at = 0, end = b->length, consumed;
    if (!gzip) {
        /* HTTP deflate is normally zlib-wrapped; older servers also send raw
         * DEFLATE. A recognizable zlib header always retains Adler checking. */
        unsigned flags = end >= 2 && (b->data[0] & 15) == 8 &&
                         (((unsigned)b->data[0] << 8) | b->data[1]) % 31 == 0
                             ? TINFL_FLAG_PARSE_ZLIB_HEADER : 0;
        if (inflate_stream(b->data, end, &out, flags, &consumed) || consumed != end)
            goto bad;
    } else do {
        unsigned start = at, begin = out.length;
        if (end - at < 18 || b->data[at] != 31 || b->data[at + 1] != 139 ||
            b->data[at + 2] != 8 || b->data[at + 3] & 0xe0)
            goto bad;
        unsigned flags = b->data[at + 3];
        at += 10;
        if (flags & 4) {
            if (end - at < 2) goto bad;
            unsigned length = b->data[at] | ((unsigned)b->data[at + 1] << 8);
            at += 2;
            if (length > end - at) goto bad;
            at += length;
        }
        for (unsigned bit = 8; bit <= 16; bit *= 2)
            if (flags & bit) {
                while (at < end && b->data[at]) at++;
                if (at == end) goto bad;
                at++;
            }
        if (flags & 2) {
            if (end - at < 2 || (crc32(b->data + start, at - start) & 65535u) !=
                (unsigned)(b->data[at] | ((unsigned)b->data[at + 1] << 8))) goto bad;
            at += 2;
        }
        if (inflate_stream(b->data + at, end - at, &out, 0, &consumed)) goto bad;
        at += consumed;
        if (end - at < 8 || little32(b->data + at) != crc32(out.data + begin, out.length - begin) ||
            little32(b->data + at + 4) != out.length - begin) goto bad;
        at += 8;
    } while (at < end); /* RFC 1952 permits concatenated gzip members. */
    sb_buffer_reset(b);
    *b = out;
    return 0;
bad:
    sb_buffer_reset(&out);
    return -1;
}
static int content_length(const char *p, uint32_t *result) {
    int first = 1;
    for (;;) {
        while (*p == ' ' || *p == '\t') p++;
        if (!isdigit((unsigned char)*p)) return -1;
        uint32_t length = 0;
        while (isdigit((unsigned char)*p)) {
            unsigned digit = *p++ - '0';
            if (length > (SB_RESOURCE_MAX - digit) / 10) return -1;
            length = length * 10 + digit;
        }
        if (!first && length != *result) return -1;
        *result = length; first = 0;
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) return 0;
        if (*p++ != ',') return -1;
    }
}
SbNetContext *SbNetCreate(void) {
    SbNetContext *c = calloc(1, sizeof(*c));
    if (c)
        c->fd = -1;
    return c;
}
void SbNetRelease(SbNetResult *r) {
    free(r->body);
    r->body = 0;
    r->body_bytes = 0;
}
void SbNetDestroy(SbNetContext *c) {
    if (c) {
        disconnect(c);
        memset(c, 0, sizeof(*c));
        free(c);
    }
}
int SbNetFetch(SbNetContext *c, const SbNetRequest *q, SbNetResult *r) {
    if (!c || !q || !r)
        return -1;
    memset(r, 0, sizeof(*r));
    r->size = sizeof(*r);
    r->version = 1;
    if (!((q->version == 1 && (q->size == SB_NET_REQUEST_V1_BYTES || q->size == sizeof(*q))) ||
          (q->version == SB_NET_REQUEST_VERSION && q->size == sizeof(*q)))) {
        disconnect(c);
        r->error = -10;
        strcpy(r->message, "Unsupported network request record version or size");
        return r->error;
    }
    c->request = q;
    disconnect(c);
    c->secure = c->tls_ready = c->io_error = 0;
    c->cancelled = 0;
    c->clock_retried = 0;
    c->timeout = q->timeout_ticks ? q->timeout_ticks : 4500;
    c->began = sb_ticks();
    unsigned flags = q->version >= 2 ? q->flags : 0;
    if (flags & ~SB_NET_REQUIRE_HTTPS)
        return fail(c, r, -10, "Unsupported network request flags");
    if (!q->url || strlen(q->url) >= sizeof(r->url) || q->body_bytes > 1024 * 1024)
        return fail(c, r, -10, "Request exceeds limit");
    if (q->body_bytes && !q->body)
        return fail(c, r, -10, "Request body is missing");
    if (q->content_type && (strlen(q->content_type) > 2048 || !token_safe(q->content_type)))
        return fail(c, r, -12, "Invalid or oversized request Content-Type");
    strcpy(r->url, q->url);
    const char *method = q->method ? q->method : "GET";
    if (strcmp(method, "GET") && strcmp(method, "POST") && strcmp(method, "HEAD"))
        return fail(c, r, -10, "Unsupported HTTP method");
    int use_body = !strcmp(method, "POST");
    for (unsigned redirects = 0; redirects <= 20; redirects++) {
        SbUrl u;
        char error[256];
        if (sb_url_parse(r->url, &u, error, sizeof(error)))
            return fail(c, r, -11, error);
        if ((flags & SB_NET_REQUIRE_HTTPS) && !u.secure)
            return fail(c, r, -19, "An HTTPS page resource redirected to an unencrypted URL");
retry_connection:
        c->secure = 0;
        if (connect_to(c, &u, r) < 0)
            return r->error;
        char header[16384], host[320];
        snprintf(host, sizeof(host), "%s:%u", u.host, u.port);
        int at = snprintf(header, sizeof(header),
                          "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: SandBrowser/" SB_VERSION
                          " (SandCore; mio)\r\nAccept: "
                          "text/html,application/xhtml+xml,text/css,image/*,*/"
                          "*;q=0.5\r\nAccept-Encoding: gzip, deflate\r\nConnection: close\r\n",
                          method, u.path, host);
        char cookies[8192] = {0};
        unsigned used = 0;
        for (unsigned i = 0; i < 64; i++) {
            Cookie *t = c->cookies + i;
            if (!t->order || strcmp(t->host, u.host) || (t->secure && !u.secure) ||
                strncmp(t->path, u.path, strlen(t->path)))
                continue;
            unsigned need = strlen(t->name) + strlen(t->value) + 3;
            if (used + need >= sizeof(cookies))
                continue;
            used += snprintf(cookies + used, sizeof(cookies) - used, "%s%s=%s", used ? "; " : "",
                             t->name, t->value);
        }
        if (used)
            at += snprintf(header + at, sizeof(header) - at, "Cookie: %s\r\n", cookies);
        if (use_body) {
            if (q->content_type && !token_safe(q->content_type))
                return fail(c, r, -12, "Invalid request header");
            at += snprintf(header + at, sizeof(header) - at,
                           "Content-Type: %s\r\nContent-Length: %u\r\n",
                           q->content_type ? q->content_type : "application/x-www-form-urlencoded",
                           q->body_bytes);
        }
        at += snprintf(header + at, sizeof(header) - at, "\r\n");
        if (at < 0 || at >= (int)sizeof(header) || stream_write(c, header, at) < 0 ||
            (use_body && stream_write(c, q->body, q->body_bytes) < 0)) {
            int tls_error = c->tls_ready ? br_ssl_engine_last_error(&c->client.eng) : 0;
            if (c->secure && !c->network_clock && !c->clock_retried &&
                (tls_error == BR_ERR_X509_EXPIRED || tls_error == BR_ERR_X509_TIME_UNKNOWN)) {
                c->clock_retried = 1;
                disconnect(c);
                if (synchronize_time(c) == 0)
                    goto retry_connection;
            }
            return fail(c, r, -13, "HTTP/TLS send failed");
        }
        char line[8192], location[SB_URL] = {0};
        int chunked = 0, encoding = 0;
        uint32_t length = 0;
        int has_length = 0;
        unsigned header_bytes = 0;
        do {
            int n = line_read(c, line, sizeof(line));
            if (n < 12 || strncmp(line, "HTTP/1.", 7) || (line[7] != '0' && line[7] != '1') ||
                line[8] != ' ' || !isdigit((unsigned char)line[9]) ||
                !isdigit((unsigned char)line[10]) || !isdigit((unsigned char)line[11]) ||
                (line[12] && line[12] != ' '))
                return fail(c, r, -14, "Invalid HTTP response or TLS validation failed");
            r->status = strtoul(line + 9, 0, 10);
            if (r->status < 100 || r->status > 599)
                return fail(c, r, -14, "Invalid HTTP status");
            for (;;) {
                int n = line_read(c, line, sizeof(line));
                if (n < 0 || (header_bytes += n + 2) > 65536)
                    return fail(c, r, -15, "Invalid or oversized HTTP headers");
                if (!n)
                    break;
                char *colon = strchr(line, ':');
                if (!colon)
                    return fail(c, r, -15, "Malformed HTTP header");
                *colon++ = 0;
                char *v = trim(colon);
                if (!strcasecmp(line, "Content-Length")) {
                    uint32_t z;
                    if (content_length(v, &z) || (has_length && length != z))
                        return fail(c, r, -16, "Invalid or oversized Content-Length");
                    has_length = 1;
                    length = z;
                } else if (!strcasecmp(line, "Transfer-Encoding")) {
                    if (strcasecmp(v, "chunked"))
                        return fail(c, r, -17, "Unsupported transfer encoding");
                    chunked = 1;
                } else if (!strcasecmp(line, "Content-Encoding")) {
                    if (!strcasecmp(v, "gzip"))
                        encoding = 1;
                    else if (!strcasecmp(v, "deflate"))
                        encoding = 2;
                    else if (strcasecmp(v, "identity"))
                        return fail(c, r, -17, "Unsupported content encoding");
                } else if (!strcasecmp(line, "Content-Type"))
                    snprintf(r->content_type, sizeof(r->content_type), "%s", v);
                else if (!strcasecmp(line, "Location")) {
                    if (strlen(v) >= sizeof(location))
                        return fail(c, r, -18, "Redirect URL too long");
                    strcpy(location, v);
                } else if (!strcasecmp(line, "Set-Cookie"))
                    set_cookie(c, &u, v);
            }
        } while (r->status >= 100 && r->status < 200 && r->status != 101);
        if (location[0] && (r->status == 301 || r->status == 302 || r->status == 303 ||
                            r->status == 307 || r->status == 308)) {
            char next[SB_URL];
            if (redirects == 20)
                return fail(c, r, -22, "The website redirected more than 20 times");
            SbUrl destination;
            char redirect_error[128];
            if (sb_url_resolve(r->url, location, next, sizeof(next)) ||
                sb_url_parse(next, &destination, redirect_error, sizeof(redirect_error)))
                return fail(c, r, -18, "The website sent an invalid redirect address");
            strcpy(r->url, next);
            if (r->status == 303 || ((r->status == 301 || r->status == 302) && use_body)) {
                method = "GET";
                use_body = 0;
            }
            disconnect(c);
            continue;
        }
        SbBuffer body = {0};
        int bad = 0;
        if (!strcmp(method, "HEAD") || r->status == 204 || r->status == 304) {
        } else if (chunked) {
            for (;;) {
                int n = line_read(c, line, sizeof(line));
                if (n <= 0) {
                    bad = 1;
                    break;
                }
                char *semi = strchr(line, ';');
                if (semi)
                    *semi = 0;
                char *e;
                char *size_text = trim(line);
                unsigned long z = strtoul(size_text, &e, 16);
                if (e == size_text || *e || z > SB_RESOURCE_MAX - body.length) {
                    bad = 1;
                    break;
                }
                if (!z) {
                    unsigned bytes = 0;
                    while ((n = line_read(c, line, sizeof(line))) > 0 && (bytes += n) < 65536) {
                    }
                    if (n < 0 || bytes >= 65536)
                        bad = 1;
                    break;
                }
                if (exact(c, &body, z) || line_read(c, line, sizeof(line)) != 0) {
                    bad = 1;
                    break;
                }
            }
        } else if (has_length)
            bad = exact(c, &body, length);
        else {
            unsigned char data[16384];
            for (;;) {
                int n = stream_read(c, data, sizeof(data));
                if (n == 0)
                    break;
                if (n < 0) {
                    int tls_error = c->secure ? br_ssl_engine_last_error(&c->client.eng) : 0;
                    /* A close-delimited HTTP response ends at transport EOF.
                     * Some HTTP/1.x servers omit TLS close_notify. This exception
                     * applies only after verified headers; explicit lengths and
                     * chunked bodies still require complete framing. */
                    if (c->secure && !stopped(c) && (tls_error == 0 ||
                        (tls_error == BR_ERR_IO && c->io_error == -32)))
                        break;
                    bad = 1;
                    break;
                }
                if (sb_buffer_append(&body, data, n)) {
                    bad = 1;
                    break;
                }
            }
        }
        if (bad) {
            sb_buffer_reset(&body);
            return fail(c, r, -20, "Incomplete or oversized HTTP body");
        }
        if (encoding && decompress_body(&body, encoding == 1)) {
            sb_buffer_reset(&body);
            return fail(c, r, -21, "Invalid compressed HTTP body");
        }
        if (!body.data)
            sb_buffer_append(&body, "", 0);
        r->body = body.data;
        r->body_bytes = body.length;
        r->secure = c->secure;
        disconnect(c);
        return 0;
    }
    return fail(c, r, -22, "The website redirected more than 20 times");
}
static ScIpcChannel owner;
static SbPacket *packet;
static uint32_t active_epoch;
static int quit, cancelled;
static SbBuffer pending;
static SbWire pending_header;
static int poll_owner(void *unused) {
    (void)unused;
    ScIpcPacket m;
    int n;
    while ((n = sb_receive(owner, packet, &m)) >= 0) {
        if (packet->h.kind == SB_SHUTDOWN) {
            quit = cancelled = 1;
            break;
        }
        if (packet->h.kind == SB_CANCEL && packet->h.epoch == active_epoch)
            cancelled = 1;
        if (packet->h.kind == SB_FETCH) {
            if (packet->h.epoch != active_epoch)
                cancelled = 1;
            if (!pending.data) {
                pending_header = packet->h;
                sb_buffer_append(&pending, packet->body, n);
            }
        }
    }
    if (n == SC32_E_BROKEN_PIPE)
        quit = cancelled = 1;
    return cancelled;
}
static int send_wait(unsigned kind, unsigned id, unsigned epoch, unsigned a, unsigned b, unsigned c,
                     unsigned d, const void *p, unsigned n) {
    int s;
    while ((s = sb_send(owner, kind, id, epoch, a, b, c, d, p, n)) == SC32_E_AGAIN) {
        if (poll_owner(0))
            return -1;
        sb_wait(owner, SC32_BROKER_READY_WRITE, 1);
    }
    return s;
}
int SbNetMain(void) {
    owner = sb_worker_connect(SB_ROLE_NET, 0);
    if (!owner)
        return 1;
    packet = malloc(sizeof(*packet));
    SbNetContext *ctx = SbNetCreate();
    if (!packet || !ctx)
        return 2;
    while (!quit) {
        ScIpcPacket m;
        int n;
        if (pending.data) {
            packet->h = pending_header;
            n = pending.length;
            memcpy(packet->body, pending.data, n);
            sb_buffer_reset(&pending);
        } else
            n = sb_receive(owner, packet, &m);
        if (n == SC32_E_AGAIN) {
            sb_wait(owner, SC32_BROKER_READY_READ, 10);
            continue;
        }
        if (n < 0)
            break;
        if (packet->h.kind == SB_SHUTDOWN)
            break;
        if (packet->h.kind != SB_FETCH)
            continue;
        SbWire h = packet->h;
        char *zero = memchr(packet->body, 0, n);
        if (!zero || zero == (char *)packet->body || zero - (char *)packet->body >= SB_URL)
            continue;
        char url[SB_URL];
        strcpy(url, (char *)packet->body);
        unsigned offset = zero - (char *)packet->body + 1;
        active_epoch = h.epoch;
        cancelled = 0;
    SbNetRequest q = {sizeof(q),
                          SB_NET_REQUEST_VERSION,
                          url,
                          h.a ? "POST" : "GET",
                          "application/x-www-form-urlencoded",
                          packet->body + offset,
                          (unsigned)n - offset,
                          4500,
                          poll_owner,
                          0,
                          h.b & SB_NET_REQUIRE_HTTPS}; /* body must survive IPC polling */
        unsigned char *post = 0;
        if (q.body_bytes) {
            post = malloc(q.body_bytes);
            if (!post)
                continue;
            memcpy(post, q.body, q.body_bytes);
            q.body = post;
        }
        SbNetResult r;
        int s = SbNetFetch(ctx, &q, &r);
        free(post);
        if (!quit && !cancelled) {
            if (s < 0) {
                char error[512];
                snprintf(error, sizeof(error), "%s [network %d, TLS %d]", r.message, r.error,
                         r.tls_error);
                send_wait(SB_ERROR, h.id, h.epoch, r.error, r.tls_error, 0, 0, error,
                          strlen(error) + 1);
            } else {
                SbBuffer head = {0};
                sb_buffer_append(&head, r.url, strlen(r.url) + 1);
                sb_buffer_append(&head, r.content_type, strlen(r.content_type) + 1);
                send_wait(SB_HEADERS, h.id, h.epoch, r.status, r.secure, r.body_bytes, 0, head.data,
                          head.length);
                sb_buffer_reset(&head);
                for (unsigned at = 0; at < r.body_bytes && !quit && !cancelled;) {
                    unsigned take = r.body_bytes - at;
                    if (take > SB_CHUNK)
                        take = SB_CHUNK;
                    if (send_wait(SB_DATA, h.id, h.epoch, at, 0, 0, 0, r.body + at, take) < 0)
                        break;
                    at += take;
                }
                send_wait(SB_DONE, h.id, h.epoch, r.status, r.secure, 0, 0, 0, 0);
            }
        }
        SbNetRelease(&r);
    }
    SbNetDestroy(ctx);
    sb_buffer_reset(&pending);
    free(packet);
    ScIpcClose(owner);
    return 0;
}
