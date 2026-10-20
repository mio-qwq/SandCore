/* Copyright (c) 2026 mio. Host checks for untrusted SNTP input and clock lifetime.
 * Uses the production parser and clock code; no live network or guest is started. */
#include "sb.h"
#include <assert.h>
#include "../src/net.c"

static ScUtcClock local;
static uint64_t monotonic = 0x100000000ull + 100;
static unsigned frequency = 100, network_attempts;
static int clock_status = SC32_E_NOT_FOUND;

void sb_record(void *p, unsigned n) {
    memset(p, 0, n);
    ((uint32_t *)p)[0] = n;
    ((uint32_t *)p)[1] = 1;
}
ScStatus ScSystemClock(ScUtcClock *out) {
    assert(out->size == sizeof(*out) && out->version == 1);
    if (!clock_status)
        *out = local;
    return clock_status;
}
ScStatus ScSystemMonotonic(ScMonotonicClock *out) {
    assert(out->size == sizeof(*out) && out->version == 1);
    out->ticks.lo = monotonic;
    out->ticks.hi = monotonic >> 32;
    out->hz = frequency;
    return SC32_OK;
}
uint32_t sb_ticks(void) { return monotonic; }
ScStatus ScSystemWait(ScU32 mask, ScU32 timeout, ScU32 *events) {
    (void)mask; (void)timeout; (void)events;
    assert(!"The offline test must not wait on a network socket");
    return SC32_E_NOT_FOUND;
}
int sb_dns_begin(const char *host) {
    (void)host;
    network_attempts++;
    return -1;
}
int sb_dns_poll(int ticket, uint32_t result[8]) { (void)ticket; (void)result; abort(); }
void sb_dns_close(int ticket) { (void)ticket; abort(); }
int sb_socket_open(int type) { (void)type; abort(); }
int sb_socket_write(int fd, const void *p, unsigned n, const uint32_t a[4]) {
    (void)fd; (void)p; (void)n; (void)a; abort();
}
int sb_socket_read(int fd, void *p, unsigned n, uint32_t a[4]) {
    (void)fd; (void)p; (void)n; (void)a; abort();
}
int sb_socket_close(int fd) { (void)fd; abort(); }
static void put32(unsigned char *out, uint32_t value) {
    out[0] = value >> 24; out[1] = value >> 16;
    out[2] = value >> 8; out[3] = value;
}
static void packet_for(unsigned char reply[48], const unsigned char nonce[8], uint64_t utc) {
    memset(reply, 0, 48);
    reply[0] = (4 << 3) | 4;
    reply[1] = 2;
    memcpy(reply + 24, nonce, 8);
    put32(reply + 40, (uint32_t)(utc + 2208988800ull));
    put32(reply + 44, 1);
}
int main(void) {
    unsigned char nonce[8] = {0x7c, 0x91, 0x33, 0xf0, 0x10, 0xa8, 0x55, 0x6d}, reply[48];
    uint64_t actual;
    const uint64_t valid[] = {1577836800ull, 2085978497ull, 4102444799ull};
    for (unsigned i = 0; i < sizeof(valid) / sizeof(*valid); i++) {
        packet_for(reply, nonce, valid[i]);
        assert(!ntp_reply(reply, 48, nonce, &actual) && actual == valid[i]);
    }
    packet_for(reply, nonce, 1577836799ull);
    assert(ntp_reply(reply, 48, nonce, &actual) < 0);
    packet_for(reply, nonce, 4102444800ull);
    assert(ntp_reply(reply, 48, nonce, &actual) < 0);
    packet_for(reply, nonce, 1792440000ull);
    assert(ntp_reply(reply, 47, nonce, &actual) < 0);
    assert(ntp_reply(reply, 513, nonce, &actual) < 0);
    reply[24] ^= 1;
    assert(ntp_reply(reply, 48, nonce, &actual) < 0); /* Unsolicited response. */
    reply[24] ^= 1;
    reply[0] |= 0xc0;
    assert(ntp_reply(reply, 48, nonce, &actual) < 0); /* Server is unsynchronized. */
    reply[0] = (4 << 3) | 3;
    assert(ntp_reply(reply, 48, nonce, &actual) < 0); /* Echoed client request. */
    reply[0] = (2 << 3) | 4;
    assert(ntp_reply(reply, 48, nonce, &actual) < 0);
    reply[0] = (4 << 3) | 4;
    reply[1] = 0;
    assert(ntp_reply(reply, 48, nonce, &actual) < 0); /* Kiss-of-death packet. */
    reply[1] = 16;
    assert(ntp_reply(reply, 48, nonce, &actual) < 0);
    reply[1] = 2;
    memset(reply + 40, 0, 8);
    assert(ntp_reply(reply, 48, nonce, &actual) < 0);

    uint32_t days, seconds;
    local.year = 2020; local.month = local.day = 1;
    clock_status = 0;
    assert(!sb_utc(&days, &seconds) && days == 737790 && seconds == 0);
    local.month = 2; local.day = 29; local.hour = 23; local.minute = local.second = 59;
    assert(!sb_utc(&days, &seconds) && days == 737849 && seconds == 86399);
    local.year = 2021;
    assert(sb_utc(&days, &seconds) < 0);
    local.year = 2020; local.second = 60;
    assert(sb_utc(&days, &seconds) < 0);
    clock_status = SC32_E_NOT_FOUND;
    assert(sb_utc(&days, &seconds) < 0);

    SbNetRequest request = {0};
    SbNetContext context = {0};
    context.request = &request;
    context.timeout = 10000;
    context.began = sb_ticks();
    context.network_clock = 1;
    context.clock_unix = 1577836800ull;
    context.clock_tick = monotonic;
    context.clock_hz = frequency;
    monotonic += 250;
    assert(!certificate_time(&context, &days, &seconds));
    assert(days == 737790 && seconds == 2 && !network_attempts);
    frequency = 200;
    assert(certificate_time(&context, &days, &seconds) < 0 && network_attempts == 3);
    frequency = 100;
    monotonic = context.clock_tick - 1;
    context.began = sb_ticks();
    assert(certificate_time(&context, &days, &seconds) < 0 && network_attempts == 6);
    monotonic = context.clock_tick + 360000;
    context.began = sb_ticks();
    assert(certificate_time(&context, &days, &seconds) < 0 && network_attempts == 9);

    unsigned char random1[32], random2[32];
    if (sb_random_sources()) {
        assert(!sb_random(random1, sizeof(random1)));
        assert(!sb_random(random2, sizeof(random2)));
        assert(memcmp(random1, random2, sizeof(random1)));
    } else {
        memset(random1, 0x5a, sizeof(random1));
        assert(sb_random(random1, sizeof(random1)) < 0);
        for (unsigned i = 0; i < sizeof(random1); i++) assert(!random1[i]);
    }
    puts("SNTP rejection, 2036 era, UTC calendar, monotonic cache and hardware RNG checks passed.");
    return 0;
}
