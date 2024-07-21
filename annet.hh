#pragma once

int listen_tcp(
  unsigned short port);

void async_accept(
  unsigned server, void (**cb)(void*, int sock));

void async_read(
  unsigned sock, char* dst, unsigned len, void (**cb)(void*, unsigned n));

void async_write(
  unsigned sock, char const* src, unsigned len, void (**cb)(void*, bool err));

void an_close(unsigned sock);

void run();
