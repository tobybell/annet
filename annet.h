#pragma once

unsigned an_resolve(char const* domain);

void an_init();

int an_listen(
  unsigned short port);

void an_accept(
  unsigned server, void (**cb)(void*, int sock));

void an_connect(
  unsigned ip, unsigned short port, void (**cb)(void*, int sock));

void an_read(
  unsigned sock, char* dst, unsigned len, void (**cb)(void*, int n));

void an_write(
  unsigned sock, char const* src, unsigned len, void (**cb)(void*, bool err));

void an_close(unsigned sock);

void an_write_done(unsigned sock);

void an_run();
