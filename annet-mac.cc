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
  void (**done)(void*, u32);
};

void try_write(i32 fd, PendingWrite* pend) {
  Str s = pend->data;
  isize write = ::write(fd, s.begin(), len(s));
  auto& cb = pend->done;
  if (write < 0) {
    if (errno == EAGAIN) {
      println("write return EAGAIN");
      write = 0;
    } else {
      println("write returning negative, errno=", errno);
      (*cb)(cb, true);
      free(pend);
      return;
    }
  } else {
    println("successfully wrote ", write);
    if (len(s) <= write) {
      (*cb)(cb, false);
      free(pend);
      return;
    }
  }
  
  pend->data.base += u32(write);
  pend->data.size -= u32(write);
  struct kevent set {uintptr_t(fd), EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, reinterpret_cast<void*>(pend)};
  timespec ts {};
  kevent(kq, &set, 1, 0, 0, &ts);
}

void try_read(i32 fd, PendingRead* pend) {
  Mut<char> s = pend->data;
  isize ret = ::read(fd, s.begin(), len(s));
  auto cb = pend->done;
  if (ret < 0) {
    if (errno == EAGAIN) {
      // pass through, wait for more input
    } else {
      println("::read returned negative");
      (*cb)(cb, 0);
      free(pend);
      return;
    }
  } else if (0 < ret) {
    (*cb)(cb, u32(ret));
    free(pend);
    return;
  } else {  // 0 means EOF
    (*cb)(cb, 0);
    free(pend);
    return;
  }

  struct kevent set {uintptr_t(fd), EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, reinterpret_cast<void*>(pend)};
  timespec ts {};
  kevent(kq, &set, 1, 0, 0, &ts);
}

void async_write(u32 fd, char const* src, u32 len, void (**cb)(void*, bool)) {
  try_write(fd, new PendingWrite {{src, len}, cb});
}

void async_read(u32 fd, char* dst, u32 len, void (**cb)(void*, u32 n)) {
  try_read(fd, new PendingRead {{dst, len}, cb});
}

int listen_tcp(u16 port) {
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

void run() {
  constexpr auto timeout_s = 5;

  for (;;) {
    struct kevent e;
    timespec ts {timeout_s, 0};
    int nev = kevent(kq, 0, 0, &e, 1, &ts);
    if (nev < 1) {
      println("No events for last ", timeout_s, " seconds.");
      continue;
    }

    if (e.filter == EVFILT_WRITE) {
      i32 fd = i32(e.ident);
      auto pending = reinterpret_cast<PendingWrite*>(e.udata);
      try_write(fd, pending);
    } else if (e.filter == EVFILT_READ) {
      auto udata = usize(e.udata);
      auto is_server = udata & 1;
      if (is_server) {
        auto cb = (void (**)(void*, int)) (udata & ~1);
        i32 f = i32(e.ident);
        sockaddr_in cli;
        socklen_t len = sizeof(cli);
        i32 conn = accept(f, (sockaddr*) &cli, &len);
        check(conn >= 0);
        set_nonblocking(conn);
        (*cb)(cb, conn);
      } else {
        i32 fd = i32(e.ident);
        auto pend = reinterpret_cast<PendingRead*>(e.udata);
        try_read(fd, pend);
      }
    } else {
      println("unknown event");
      abort();
    }
  }

}

void async_accept(unsigned server, void (**cb)(void*, int sock)) {
  println("async_accept ", server, ' ', (void*) cb);
  sockaddr_in cli;
  socklen_t len = sizeof(cli);
  int now = accept(server, (sockaddr*) &cli, &len);
  if (now >= 0 || errno != EWOULDBLOCK)
    return (*cb)(cb, now);
  struct kevent set {
    .ident = uintptr_t(server),
    .filter = EVFILT_READ,
    .flags = EV_ADD | EV_ONESHOT,
    .udata = (void*) (usize(cb) | 1),
  };
  timespec ts {};
  kevent(kq, &set, 1, 0, 0, &ts);
}
