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

enum Operation : u32 {
  Read,
  Write,
  Accept,
  Connect,
};

struct PendingOperation {
  u32 op;
  u32 len;
  char* buf;
  void (**cb)(void*, int);
};

struct Socket {
  u32 fd;
  u32 read;
  u32 write;
};

template <class T>
struct FreeList {
  List<T> list;
  u32 free_head {};

  u32 alloc() {
    if (free_head) {
      u32 id = free_head - 1;
      free_head = reinterpret_cast<u32&>(list[id]);
      return id;
    } else {
      u32 id = len(list);
      list.emplace();
      return id;
    }
  }

  void free(u32 id) {
    reinterpret_cast<u32&>(list[id]) = free_head;
    free_head = id + 1;
  }

  T& operator[](u32 id) {
    return list[id];
  }
};

FreeList<PendingOperation> pending;
FreeList<Socket> sockets;

void set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void try_write(u32 sock, u32 op) {
  i32 fd = sockets[sock].fd;
  auto p = pending[op];
  auto cb = (void (**)(void*, bool)) p.cb;

  isize r = ::write(fd, p.buf, p.len);
  if (r >= 0) {
    if (p.len <= r) {
      sockets[sock].write = 0;
      pending.free(op);
      return (*p.cb)(p.cb, false);
    }
    pending[op].buf += r;
    pending[op].len -= r;
  } else if (errno != EAGAIN) {
    sockets[sock].write = 0;
    pending.free(op);
    return (*p.cb)(p.cb, true);
  }
  struct kevent set {uintptr_t(fd), EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, (void*) usize(sock)};
  timespec ts {};
  kevent(kq, &set, 1, 0, 0, &ts);
}

void try_read(u32 sock, u32 op) {
  i32 fd = sockets[sock].fd;
  PendingOperation p = pending[op];

  isize r = ::read(fd, p.buf, p.len);
  if (r >= 0) {
    sockets[sock].read = 0;
    pending.free(op);
    return (*p.cb)(p.cb, r);
  } else if (errno != EAGAIN) {
    sockets[sock].read = 0;
    pending.free(op);
    return (*p.cb)(p.cb, -1);
  }
  struct kevent set {uintptr_t(fd), EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, (void*) usize(sock)};
  timespec ts {};
  kevent(kq, &set, 1, 0, 0, &ts);
}

void try_accept(u32 sock, u32 op) {
  sockaddr_in cli;
  socklen_t len = sizeof(cli);
  println("try_accept sock=", sock, " op=", op);

  u32 fd = sockets[sock].fd;
  auto cb = pending[op].cb;

  int r = accept(fd, (sockaddr*) &cli, &len);
  if (r >= 0) {
    sockets[sock].read = 0;
    pending.free(op);
    set_nonblocking(r);
    u32 newsock = sockets.alloc();
    sockets[newsock] = {u32(r)};
    return (*cb)(cb, newsock);
  } else if (errno != EWOULDBLOCK) {
    println("r=", r, " errno=", errno);
    sockets[sock].read = 0;
    pending.free(op);
    return (*cb)(cb, -1);
  }
  println("accept yielded");
  struct kevent set {
    .ident = uintptr_t(fd),
    .filter = EVFILT_READ,
    .flags = EV_ADD | EV_ONESHOT,
    .udata = (void*) usize(sock),
  };
  timespec ts {};
  kevent(kq, &set, 1, 0, 0, &ts);
}

void an_accept(unsigned server, void (**cb)(void*, int sock)) {
  println("an_accept server=", server);
  check(!sockets[server].read);
  u32 op = pending.alloc();
  pending[op] = {Accept, 0, 0, cb};
  sockets[server].read = op + 1;
  try_accept(server, op);
}

void an_write(u32 sock, char const* src, u32 len, void (**cb)(void*, bool)) {
  check(!sockets[sock].write);
  u32 op = pending.alloc();
  pending[op] = {Write, len, (char*) src, (void (**)(void*, int)) cb};
  sockets[sock].write = op + 1;
  try_write(sock, op);
}

void an_read(u32 sock, char* dst, u32 len, void (**cb)(void*, int n)) {
  check(!sockets[sock].read);
  u32 op = pending.alloc();
  pending[op] = {Read, len, dst, cb};
  sockets[sock].read = op + 1;
  try_read(sock, op);
}

int an_listen(u16 port) {
  if (!kq)
    kq = kqueue();

  int r = socket(AF_INET, SOCK_STREAM, 0);
  if (r < 0)
    return -1;

  u32 acceptor = u32(r);
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

  u32 sock = sockets.alloc();
  sockets[sock] = {acceptor};
  return sock;
}

void an_close(unsigned sock) {
  check(sockets[sock].fd != ~0u);
  Socket s = sockets[sock];
  sockets[sock].fd = ~0u;
  if (s.read) {
    u32 op = s.read - 1;
    auto cb = pending[op].cb;
    pending.free(op);
    (*cb)(cb, -1);
  }
  if (s.write) {
    u32 op = s.write - 1;
    auto cb = (void (**)(void*, bool)) pending[op].cb;
    pending.free(op);
    (*cb)(cb, true);
  }
  close(s.fd);
  sockets.free(sock);
}

void handle_event(u32 sock, bool write) {
  println("got sock=", sock, write ? " write" : " read");
  if (!write) {
    u32 op = sockets[sock].read;
    if (!op)
      return println("sock=", sock, " found no valid read op");
    --op;
    println("sock=", sock, " had valid read op=", op);
    if (pending[op].op == Accept) {
      println("read op was an accept");
      return try_accept(sock, op);
    } else if (pending[op].op == Read) {
      println("read op was a read");
      return try_read(sock, op);
    } else {
      println("read op was some unknown thing");
      abort();
    }
  } else {
    u32 op = sockets[sock].write;
    if (!op)
      return println("sock=", sock, " found no valid write op");
    --op;
    println("sock=", sock, " had valid write op=", op);
    if (pending[op].op == Write) {
      println("read op was a write");
      return try_write(sock, op);
    } else {
      println("write op was some unknown thing");
      abort();
    }
  }
}

void an_run() {
  for (;;) {
    struct kevent e;
    int nev = kevent(kq, 0, 0, &e, 1, 0);
    check(nev >= 0);

    if (e.filter == EVFILT_WRITE) {
      handle_event(u32(usize(e.udata)), true);
    } else if (e.filter == EVFILT_READ) {
      handle_event(u32(usize(e.udata)), false);
    } else {
      abort();
    }
  }
}
