/* Copyright (c) 2026 mio. Host-only socket adapter for the real SBNET engine.
 * This is never linked into a shipped SCX/SCL. No proxy or host code is needed
 * by the installed browser. Build with net.c, common.c, entropy.c and url.c. */
#define _POSIX_C_SOURCE 200809L
#include "sb.h"
#include "SBNET.H"
#include "sb_transport.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static uint32_t dns_address;
static const char *fault;
static int inject(const char *name) { return fault && !strcmp(fault, name); }
int sb_socket_open(int type) {
    if (inject("socket")) return -24;
    int fd = socket(AF_INET, type == 2 ? SOCK_DGRAM : SOCK_STREAM, 0);
    if (fd < 0) return -errno;
    if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) { int error = errno; close(fd); return -error; }
    return fd;
}
static struct sockaddr_in address_of(const uint32_t a[4]) {
    struct sockaddr_in result = {0};
    result.sin_family = AF_INET;
    result.sin_addr.s_addr = htonl(a[1]);
    result.sin_port = htons(a[2]);
    return result;
}
int sb_socket_connect(int fd, const uint32_t a[4]) {
    if (inject("connect")) return -111;
    struct sockaddr_in destination = address_of(a);
    int status = connect(fd, (struct sockaddr *)&destination, sizeof(destination));
    return status < 0 ? -errno : 0;
}
int sb_socket_read(int fd, void *p, unsigned n, uint32_t a[4]) {
    if (inject("read")) return -104;
    struct sockaddr_in source;
    socklen_t length = sizeof(source);
    int status = recvfrom(fd, p, n, 0, (struct sockaddr *)&source, &length);
    if (status >= 0 && a) {
        a[0] = 1; a[1] = ntohl(source.sin_addr.s_addr);
        a[2] = ntohs(source.sin_port); a[3] = 0;
    }
    return status < 0 ? -errno : status;
}
int sb_socket_write(int fd, const void *p, unsigned n, const uint32_t a[4]) {
    if (inject("write")) return -32;
    struct sockaddr_in destination;
    if (a) destination = address_of(a);
    int status = sendto(fd, p, n, MSG_NOSIGNAL, a ? (struct sockaddr *)&destination : 0,
                        a ? sizeof(destination) : 0);
    return status < 0 ? -errno : status;
}
int sb_socket_status(int fd, uint32_t state[16]) {
    struct pollfd wait = {fd, POLLOUT, 0};
    int ready = poll(&wait, 1, 0);
    if (ready < 0) return -errno;
    if (ready) {
        int error = 0;
        socklen_t length = sizeof(error);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length)) return -errno;
        state[4] = error ? -error : 0;
        if (wait.revents & POLLOUT) state[3] = 2;
    }
    return 0;
}
int sb_socket_close(int fd) { return close(fd) < 0 ? -errno : 0; }
int sb_dns_begin(const char *name) {
    if (inject("dns")) return -2;
    struct addrinfo hint = {0}, *result;
    hint.ai_family = AF_INET; hint.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(name, 0, &hint, &result)) return -2;
    dns_address = ntohl(((struct sockaddr_in *)result->ai_addr)->sin_addr.s_addr);
    freeaddrinfo(result);
    return 1;
}
int sb_dns_poll(int ticket, uint32_t a[8]) {
    (void)ticket; a[1] = 2; a[2] = dns_address; return 0;
}
void sb_dns_close(int ticket) { (void)ticket; }
ScStatus ScSystemMonotonic(ScMonotonicClock *out) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) return -1;
    uint64_t ticks = (uint64_t)now.tv_sec * 100 + now.tv_nsec / 10000000;
    out->ticks.lo = ticks; out->ticks.hi = ticks >> 32; out->hz = 100;
    return 0;
}
ScStatus ScSystemClock(ScUtcClock *out) {
    time_t now = time(0);
    struct tm utc;
    if (!gmtime_r(&now, &utc)) return -1;
    out->year = utc.tm_year + 1900; out->month = utc.tm_mon + 1; out->day = utc.tm_mday;
    out->hour = utc.tm_hour; out->minute = utc.tm_min; out->second = utc.tm_sec;
    out->utc_offset_seconds = 0;
    return 0;
}
ScStatus ScSystemWait(ScU32 mask, ScU32 timeout, ScU32 *events) {
    (void)mask; (void)timeout;
    poll(0, 0, 10);
    if (events) *events = 0;
    return 0;
}
static void json_string(const char *p) {
    putchar('"');
    for (; *p; p++) {
        unsigned char c = *p;
        if (c == '"' || c == '\\') { putchar('\\'); putchar(c); }
        else if (c < 32 || c >= 127) printf("\\u%04x", c);
        else putchar(c);
    }
    putchar('"');
}
int main(int argc, char **argv) {
    if (argc < 2) return 2;
    fault = getenv("SB_HOST_FAULT");
    SbNetContext *ctx = SbNetCreate();
    if (!ctx) return 3;
    SbNetRequest q = {0};
    q.size = sizeof(q); q.version = SB_NET_REQUEST_VERSION; q.url = argv[1];
    q.flags = argc > 2 ? strtoul(argv[2], 0, 0) : 0;
    q.method = argc > 3 ? argv[3] : "GET";
    q.body = argc > 4 ? argv[4] : "";
    q.body_bytes = strlen(q.body); q.timeout_ticks = 2000;
    q.content_type = "application/x-www-form-urlencoded";
    if (inject("record")) q.size = 8;
    if (inject("body")) { q.body = 0; q.body_bytes = 1; }
    if (inject("header")) q.content_type = "text/plain\r\nInjected: yes";
    if (argc > 5 && !strcmp(argv[5], "v1")) { q.version = 1; q.size = SB_NET_REQUEST_V1_BYTES; }
    SbNetResult r;
    int status = SbNetFetch(ctx, &q, &r);
    uint32_t hash = 2166136261u;
    for (unsigned i = 0; i < r.body_bytes; i++) hash = (hash ^ r.body[i]) * 16777619u;
    printf("{\"result\":%d,\"error\":%d,\"tls\":%d,\"status\":%u,\"secure\":%u,\"bytes\":%u,\"hash\":%u,\"url\":",
           status, r.error, r.tls_error, r.status, r.secure, r.body_bytes, hash);
    json_string(r.url); printf(",\"message\":"); json_string(r.message); puts("}");
    SbNetRelease(&r); SbNetDestroy(ctx);
    return 0;
}
