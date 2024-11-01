extern "C" {
#include "annet.h"

unsigned raw_accept(unsigned server);
unsigned raw_connect(unsigned ip, unsigned short port);
unsigned raw_read(unsigned sock, char* dst, unsigned len);
unsigned raw_write(unsigned sock, char const* src, unsigned len);

}

#include "common.hh"

static List<void (**)(void*, int)> pend;

static void set_callback(unsigned op, void (**cb)(void*, int)) {
  if (len(pend) <= op) {
    check(len(pend) == op);
    pend.push(cb);
  } else
    pend[op] = cb;
}

void an_accept(unsigned server, void (**cb)(void*, int sock)) {
  unsigned op = raw_accept(server);
  set_callback(op, cb);
}

void an_connect(unsigned ip, unsigned short port, void (**cb)(void*, int sock)) {
  unsigned op = raw_connect(ip, port);
  set_callback(op, cb);
}

void an_read(unsigned sock, char* dst, unsigned len, void (**cb)(void*, int n)) {
  unsigned op = raw_read(sock, dst, len);
  set_callback(op, cb);
}

void an_write(unsigned sock, char const* src, unsigned len, void (**cb)(void*, int err)) {
  unsigned op = raw_write(sock, src, len);
  set_callback(op, cb);
}

