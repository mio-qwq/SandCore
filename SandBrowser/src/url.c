/* Copyright (c) 2026 mio. 通用 URL 解析、相对地址与来源比较。 */
#include "sb.h"
#include <ctype.h>
static int copy(char *o, size_t n, const char *s, size_t len) {
    if (len >= n)
        return -1;
    memcpy(o, s, len);
    o[len] = 0;
    return 0;
}
int sb_url_parse(const char *input, SbUrl *u, char *error, size_t cap) {
    memset(u, 0, sizeof(*u));
    const char *p = strstr(input, "://");
    if (!p) {
        snprintf(error, cap, "An absolute http:// or https:// URL is required.");
        return -1;
    }
    if (copy(u->scheme, sizeof(u->scheme), input, p - input))
        goto bad;
    for (char *q = u->scheme; *q; q++)
        *q = tolower((unsigned char)*q);
    if (strcmp(u->scheme, "https") && strcmp(u->scheme, "http"))
        goto bad;
    u->secure = !strcmp(u->scheme, "https");
    u->port = u->secure ? 443 : 80;
    p += 3;
    const char *end = p;
    while (*end && *end != '/' && *end != '?' && *end != '#')
        end++;
    const char *colon = 0;
    for (const char *q = p; q < end; q++) {
        if (*q == ':' && !colon)
            colon = q;
        if (*q == '@' || *q == '[' || *q == ']' || (unsigned char)*q <= 32)
            goto bad;
    }
    if (copy(u->host, sizeof(u->host), p, (colon ? colon : end) - p) || !u->host[0])
        goto bad;
    for (char *q = u->host; *q; q++)
        *q = tolower((unsigned char)*q);
    if (colon) {
        char port[8];
        if (copy(port, sizeof(port), colon + 1, end - colon - 1))
            goto bad;
        char *tail;
        unsigned long n = strtoul(port, &tail, 10);
        if (!port[0] || *tail || !n || n > 65535)
            goto bad;
        u->port = n;
    }
    const char *hash = strchr(end, '#');
    size_t len = hash ? (size_t)(hash - end) : strlen(end);
    if (*end == '?') {
        u->path[0] = '/';
        if (copy(u->path + 1, sizeof(u->path) - 1, end, len))
            goto bad;
    } else if (!len)
        strcpy(u->path, "/");
    else if (copy(u->path, sizeof(u->path), end, len))
        goto bad;
    char encoded[SB_URL];
    size_t used = 0;
    const char hex[] = "0123456789ABCDEF";
    for (const unsigned char *q = (const unsigned char *)u->path; *q; q++) {
        if (*q < 32) goto bad;
        int escape = *q == ' ' || *q >= 127 || *q == '"' || *q == '<' || *q == '>' || *q == '`';
        if (used + (escape ? 3 : 1) >= sizeof(encoded)) goto bad;
        if (escape) {
            encoded[used++] = '%'; encoded[used++] = hex[*q >> 4]; encoded[used++] = hex[*q & 15];
        } else encoded[used++] = *q;
    }
    encoded[used] = 0;
    strcpy(u->path, encoded);
    snprintf(u->origin, sizeof(u->origin), "%s://%s:%u", u->scheme, u->host, u->port);
    return 0;
bad:
    snprintf(error, cap, "Invalid or unsupported URL. This build supports HTTP/HTTPS over IPv4.");
    return -1;
}
int sb_url_resolve(const char *base, const char *relative, char *out, size_t n) {
    if (!relative)
        return -1;
    while (*relative == ' ' || *relative == '\t' || *relative == '\n')
        relative++;
    if (strstr(relative, "://") || !strncmp(relative, "about:", 6) ||
        !strncmp(relative, "file:", 5))
        return copy(out, n, relative, strlen(relative));
    if (!strncmp(base, "file:", 5) || base[0] == '/') {
        if (relative[0] == '/')
            return copy(out, n, relative, strlen(relative));
        const char *s = strrchr(base, '/');
        size_t k = s ? (size_t)(s - base + 1) : 0;
        if (k + strlen(relative) >= n)
            return -1;
        memcpy(out, base, k);
        strcpy(out + k, relative);
        return 0;
    }
    SbUrl u;
    char err[128];
    if (sb_url_parse(base, &u, err, sizeof(err)))
        return -1;
    char prefix[320];
    snprintf(prefix, sizeof(prefix), "%s://%s%s", u.scheme, u.host, "");
    if (u.port != (unsigned)(u.secure ? 443 : 80)) {
        size_t k = strlen(prefix);
        snprintf(prefix + k, sizeof(prefix) - k, ":%u", u.port);
    }
    char path[SB_URL];
    int path_length;
    if (!strncmp(relative, "//", 2)) {
        int k = snprintf(out, n, "%s:%s", u.scheme, relative);
        return k < 0 || (size_t)k >= n ? -1 : 0;
    }
    if (relative[0] == '#') {
        int k = snprintf(out, n, "%s%s%s", prefix, u.path, relative);
        return k < 0 || (size_t)k >= n ? -1 : 0;
    }
    if (!relative[0]) {
        int k = snprintf(out, n, "%s%s", prefix, u.path);
        return k < 0 || (size_t)k >= n ? -1 : 0;
    }
    char *base_query = strchr(u.path, '?');
    if (base_query)
        *base_query = 0;
    if (relative[0] == '?') {
        char *q = strchr(u.path, '?');
        if (q)
            *q = 0;
        path_length = snprintf(path, sizeof(path), "%s%s", u.path, relative);
    } else if (relative[0] == '/')
        path_length = snprintf(path, sizeof(path), "%s", relative);
    else {
        char *q = strrchr(u.path, '/');
        if (q)
            q[1] = 0;
        path_length = snprintf(path, sizeof(path), "%s%s", u.path, relative);
    }
    if (path_length < 0 || (size_t)path_length >= sizeof(path))
        return -1;
    char clean[SB_URL];
    size_t used = 0;
    const char *p = path;
    clean[used++] = '/';
    while (*p) {
        while (*p == '/')
            p++;
        if (!*p)
            break;
        const char *q = p;
        while (*q && *q != '/' && *q != '?' && *q != '#')
            q++;
        size_t z = q - p;
        if (z == 1 && p[0] == '.') {
        } else if (z == 2 && p[0] == '.' && p[1] == '.') {
            if (used > 1) {
                used--;
                while (used > 1 && clean[used - 1] != '/')
                    used--;
            }
        } else {
            if (used > 1 && clean[used - 1] != '/')
                clean[used++] = '/';
            if (used + z >= sizeof(clean))
                return -1;
            memcpy(clean + used, p, z);
            used += z;
        }
        if (*q == '?' || *q == '#') {
            size_t z2 = strlen(q);
            if (used + z2 >= sizeof(clean))
                return -1;
            memcpy(clean + used, q, z2);
            used += z2;
            break;
        }
        p = q;
    }
    if (path[strlen(path) - 1] == '/' && used > 1 && clean[used - 1] != '/')
        clean[used++] = '/';
    clean[used] = 0;
    int k = snprintf(out, n, "%s%s", prefix, clean);
    return k < 0 || (size_t)k >= n ? -1 : 0;
}
int sb_same_origin(const char *a, const char *b) {
    SbUrl x, y;
    char e[128];
    return !sb_url_parse(a, &x, e, sizeof(e)) && !sb_url_parse(b, &y, e, sizeof(e)) &&
           !strcmp(x.origin, y.origin);
}
void sb_percent_encode(const char *s, char *o, size_t n, int form) {
    const char hex[] = "0123456789ABCDEF";
    size_t k = 0;
    for (; *s; s++) {
        unsigned c = (unsigned char)*s;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            if (k + 1 >= n)
                break;
            o[k++] = c;
        } else if (form && c == ' ') {
            if (k + 1 >= n)
                break;
            o[k++] = '+';
        } else {
            if (k + 3 >= n)
                break;
            o[k++] = '%';
            o[k++] = hex[c >> 4];
            o[k++] = hex[c & 15];
        }
    }
    if (n)
        o[k] = 0;
}
