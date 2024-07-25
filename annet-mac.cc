extern "C" {
#include "annet.h"
}

#include "common.hh"
#include "print.hh"

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <unistd.h>

int kq;

void set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

struct PendingWrite {
  Str data;
  void (**done)(void*, bool);
};

struct PendingRead {
  Mut<char> data;
  void (**done)(void*, int);
};

void try_write(i32 fd, PendingWrite* pend) {
  Str s = pend->data;
  isize write = ::write(fd, s.begin(), len(s));
  auto& cb = pend->done;
  if (write >= 0) {
    if (len(s) <= write) {
      (*cb)(cb, false);
      free(pend);
      return;
    }
    pend->data.base += u32(write);
    pend->data.size -= u32(write);
  } else if (errno != EAGAIN) {
    (*cb)(cb, true);
    free(pend);
    return;
  }
  struct kevent set {uintptr_t(fd), EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, reinterpret_cast<void*>(pend)};
  timespec ts {};
  kevent(kq, &set, 1, 0, 0, &ts);
}

void try_read(i32 fd, PendingRead* pend) {
  Mut<char> s = pend->data;
  isize ret = ::read(fd, s.begin(), len(s));
  auto cb = pend->done;
  if (ret >= 0) {
    (*cb)(cb, u32(ret));
    return free(pend);
  } else if (errno != EAGAIN) {
    (*cb)(cb, -1);
    return free(pend);
  }
  struct kevent set {uintptr_t(fd), EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, reinterpret_cast<void*>(pend)};
  timespec ts {};
  kevent(kq, &set, 1, 0, 0, &ts);
}

void try_accept(unsigned fd, void (**cb)(void*, int sock)) {
  sockaddr_in cli;
  socklen_t len = sizeof(cli);
  int r = accept(fd, (sockaddr*) &cli, &len);
  if (r < 0 && errno != EWOULDBLOCK)
    return (*cb)(cb, -1);
  if (r >= 0) {
    set_nonblocking(r);
    return (*cb)(cb, r);
  }
  struct kevent set {
    .ident = uintptr_t(fd),
    .filter = EVFILT_READ,
    .flags = EV_ADD | EV_ONESHOT,
    .udata = (void*) (usize(cb) | 1),
  };
  timespec ts {};
  kevent(kq, &set, 1, 0, 0, &ts);
}

void an_accept(unsigned server, void (**cb)(void*, int sock)) {
  try_accept(server, cb);
}

void an_write(u32 fd, char const* src, u32 len, void (**cb)(void*, bool)) {
  try_write(fd, new PendingWrite {{src, len}, cb});
}

void an_read(u32 fd, char* dst, u32 len, void (**cb)(void*, int n)) {
  try_read(fd, new PendingRead {{dst, len}, cb});
}

int an_listen(u16 port) {
  if (!kq)
    kq = kqueue();

  int acceptor = socket(AF_INET, SOCK_STREAM, 0);
  check(acceptor >= 0);
  set_nonblocking(acceptor);

  sockaddr_in servaddr;
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  servaddr.sin_port = htons(port);

  if (bind(acceptor, (sockaddr*)&servaddr, sizeof(servaddr))) {
    close(acceptor);
    return -1;
  }

  if (listen(acceptor, 8)) {
    close(acceptor);
    return -1;
  }

  return acceptor;
}

void an_close(unsigned sock) {
  check(!close(i32(sock)));
}

void an_run() {
  for (;;) {
    struct kevent e;
    int nev = kevent(kq, 0, 0, &e, 1, 0);
    check(nev >= 0);

    if (e.filter == EVFILT_WRITE) {
      i32 fd = i32(e.ident);
      auto pending = reinterpret_cast<PendingWrite*>(e.udata);
      try_write(fd, pending);
    } else if (e.filter == EVFILT_READ) {
      auto udata = usize(e.udata);
      auto is_server = udata & 1;
      if (is_server) {
        auto cb = (void (**)(void*, int)) (udata & ~1);
        i32 fd = i32(e.ident);
        try_accept(fd, cb);
      } else {
        i32 fd = i32(e.ident);
        auto pend = reinterpret_cast<PendingRead*>(e.udata);
        try_read(fd, pend);
      }
    } else {
      abort();
    }
  }
}
