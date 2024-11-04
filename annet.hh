#pragma once

extern "C" {
#include "annet.h"
}

namespace an {

template <class... Arg>
struct Callback {
  void (**f)(void*, Arg...);

  template <class T>
  struct Record {
    void (*f)(void*, Arg...);
    T val;
  };

  template <class T>
  Callback(T&& t):
    f(&(new Record<T> {
           [](void* p, Arg... arg) {
             auto rec = (Record<T>*) p;
             rec->val(arg...);
             delete rec;
           },
           static_cast<T&&>(t)})
          ->f) {}
};

inline void accept(unsigned server, Callback<int> cb) {
  an_accept(server, cb.f);
}

inline void connect(unsigned ip, unsigned short port, Callback<int> cb) {
  an_connect(ip, port, cb.f);
}

inline void read(unsigned sock, char* dst, unsigned len, Callback<int> cb) {
  an_read(sock, dst, len, cb.f);
}

inline void write(unsigned sock, char const* src, unsigned len, Callback<bool> cb) {
  an_write(sock, src, len, cb.f);
}

constexpr auto init = an_init;

constexpr auto resolve = an_resolve;

constexpr auto listen = an_listen;

constexpr auto close = an_close;

constexpr auto write_done = an_write_done;

constexpr auto run = an_run;

}
