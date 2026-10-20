/* Copyright (c) 2026 mio. The production transport uses only the existing ABI. */
#include "sb_transport.h"
extern int sb_raw(int, int, int, int, int, int, int);
int sb_socket_open(int type) { return sb_raw(0x252, type, 0, 0, 0, 0, 0); }
int sb_socket_connect(int fd, const uint32_t address[4]) {
    return sb_raw(0x254, fd, (int)(uintptr_t)address, 0, 0, 0, 0);
}
int sb_socket_read(int fd, void *data, unsigned bytes, uint32_t source[4]) {
    return sb_raw(0x258, fd, (int)(uintptr_t)data, bytes, (int)(uintptr_t)source, 0, 0);
}
int sb_socket_write(int fd, const void *data, unsigned bytes, const uint32_t destination[4]) {
    return sb_raw(0x257, fd, (int)(uintptr_t)data, bytes, (int)(uintptr_t)destination, 0, 0);
}
int sb_socket_status(int fd, uint32_t state[16]) {
    return sb_raw(0x25b, fd, (int)(uintptr_t)state, 0, 0, 0, 0);
}
int sb_socket_close(int fd) { return sb_raw(0x25a, fd, 0, 0, 0, 0, 0); }
int sb_dns_begin(const char *hostname) {
    return sb_raw(0x260, (int)(uintptr_t)hostname, 0, 0, 0, 0, 0);
}
int sb_dns_poll(int ticket, uint32_t result[8]) {
    return sb_raw(0x261, ticket, (int)(uintptr_t)result, 0, 0, 0, 0);
}
void sb_dns_close(int ticket) { sb_raw(0x262, ticket, 0, 0, 0, 0, 0); }
