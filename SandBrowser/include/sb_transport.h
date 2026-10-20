/* Copyright (c) 2026 mio. Typed boundary to the existing SandCore network ABI. */
#ifndef SANDBROWSER_TRANSPORT_H
#define SANDBROWSER_TRANSPORT_H
#include <stdint.h>
int sb_socket_open(int type);
int sb_socket_connect(int fd, const uint32_t address[4]);
int sb_socket_read(int fd, void *data, unsigned bytes, uint32_t source[4]);
int sb_socket_write(int fd, const void *data, unsigned bytes, const uint32_t destination[4]);
int sb_socket_status(int fd, uint32_t state[16]);
int sb_socket_close(int fd);
int sb_dns_begin(const char *hostname);
int sb_dns_poll(int ticket, uint32_t result[8]);
void sb_dns_close(int ticket);
#endif
