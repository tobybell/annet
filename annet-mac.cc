extern "C" {
#include "annet.h"
}

#include "common.hh"

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

int kq;

enum Direction: u8 {
  ReadD, WriteD
};

struct PendingOperation {
  u32 op;
  u32 len;
  char* buf;
  void (**cb)(void*, int);
};

struct Socket {
  u32 fd;
  u32 op[2];
};

template <class T>
struct FreeList {
  List<T> list;
  u32 free_head {};
  u32 count {};

  u32 alloc(T&& val) {
    ++count;
    if (free_head) {
      u32 id = free_head - 1;
      free_head = reinterpret_cast<u32&>(list[id]);
      list[id] = move(val);
      return id;
    } else {
      u32 id = len(list);
      list.emplace() = move(val);
      return id;
    }
  }

  void free(u32 id) {
    --count;
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

void complete(u32 sock, Direction dir, int result) {
  u32 op = sockets[sock].op[dir] - 1;
  sockets[sock].op[dir] = 0;
  auto cb = pending[op].cb;
  pending.free(op);
  return (*cb)(cb, result);
}

void add_filter(u32 fd, int16_t filt, usize udata) {
  struct kevent set {usize(fd), filt, EV_ADD | EV_ONESHOT, 0, 0, (void*) udata};
  timespec ts {};
  kevent(kq, &set, 1, 0, 0, &ts);
}

void try_write(u32 sock, u32 op) {
  i32 fd = sockets[sock].fd;
  auto p = pending[op];
  auto cb = (void (**)(void*, bool)) p.cb;

  isize r = ::write(fd, p.buf, p.len);
  if (r >= 0) {
    if (p.len <= r)
      return complete(sock, WriteD, 0);
    pending[op].buf += r;
    pending[op].len -= r;
  } else if (errno != EAGAIN)
    return complete(sock, WriteD, 1);
  add_filter(fd, EVFILT_WRITE, sock);
}

void try_read(u32 sock, u32 op) {
  i32 fd = sockets[sock].fd;
  PendingOperation p = pending[op];

  isize r = ::read(fd, p.buf, p.len);
  if (r >= 0)
    return complete(sock, ReadD, r);
  else if (errno != EAGAIN)
    return complete(sock, ReadD, -1);
  add_filter(fd, EVFILT_READ, sock);
}

void try_accept(u32 sock, u32 op) {
  sockaddr_in cli;
  socklen_t len = sizeof(cli);

  u32 fd = sockets[sock].fd;
  auto cb = pending[op].cb;

  int r = accept(fd, (sockaddr*) &cli, &len);
  if (r >= 0) {
    set_nonblocking(r);
    u32 newsock = sockets.alloc({u32(r)});
    return complete(sock, ReadD, newsock);
  } else if (errno != EWOULDBLOCK)
    return complete(sock, ReadD, -1);
  add_filter(fd, EVFILT_READ, sock);
}

void try_connect(u32 sock, u32 op) {
  u32 fd = sockets[sock].fd;

  u32 ip = pending[op].op;
  u16 port = pending[op].len;

  sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(ip);
  addr.sin_port = htons(port);

  int r = ::connect(fd, (sockaddr*) &addr, sizeof(addr));
  if (r == 0 || errno == EISCONN)
    return complete(sock, WriteD, sock);
  if (errno != EINPROGRESS)
    return an_close(sock);
  add_filter(fd, EVFILT_WRITE, sock);
}

}  // namespace

void write_cerr(Str str) {
  check(::write(2, str.base, str.size) == str.size);
}

void an_init() {}

void an_connect(unsigned ip, unsigned short port, void (**cb)(void*, int sock)) {
  if (!kq)
    kq = kqueue();

  u32 fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    return (*cb)(cb, -1);

  set_nonblocking(fd);

  u32 sock = sockets.alloc({fd});
  u32 op = pending.alloc({ip, port, 0, cb});
  sockets[sock].op[WriteD] = op + 1;
  try_connect(sock, op);
}

void an_accept(unsigned server, void (**cb)(void*, int sock)) {
  check(!sockets[server].op[ReadD]);
  u32 op = pending.alloc({1, 0, 0, cb});
  sockets[server].op[ReadD] = op + 1;
  try_accept(server, op);
}

void an_write(u32 sock, char const* src, u32 len, void (**cb)(void*, bool)) {
  check(!sockets[sock].op[WriteD]);
  u32 op = pending.alloc({0, len, (char*) src, (void (**)(void*, int)) cb});
  sockets[sock].op[WriteD] = op + 1;
  try_write(sock, op);
}

void an_write_done(u32 sock) {
  check(!sockets[sock].op[WriteD]);
  u32 fd = sockets[sock].fd;
  check(!::shutdown(fd, SHUT_WR));
}

void an_read(u32 sock, char* dst, u32 len, void (**cb)(void*, int n)) {
  check(!sockets[sock].op[ReadD]);
  u32 op = pending.alloc({0, len, dst, cb});
  sockets[sock].op[ReadD] = op + 1;
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

  return sockets.alloc({acceptor});
}

void an_close(unsigned sock) {
  check(sockets[sock].fd != ~0u);
  Socket s = sockets[sock];
  sockets[sock].fd = ~0u;
  if (s.op[ReadD]) {
    u32 op = s.op[ReadD] - 1;
    auto cb = pending[op].cb;
    pending.free(op);
    (*cb)(cb, -1);
  }
  if (s.op[WriteD]) {
    u32 op = s.op[WriteD] - 1;
    auto cb = (void (**)(void*, bool)) pending[op].cb;
    pending.free(op);
    (*cb)(cb, true);
  }
  close(s.fd);
  sockets.free(sock);
}

void handle_event(u32 sock, Direction dir) {
  u32 op = sockets[sock].op[dir];
  if (!op--)
    return;
  bool server = pending[op].op;
  if (dir) {
    if (server)
      return try_connect(sock, op);
    return try_write(sock, op);
  } else {
    if (server)
      return try_accept(sock, op);
    return try_read(sock, op);
  }
}

void an_run() {
  for (;;) {
    if (!pending.count)
      return;

    struct kevent e;
    int nev = kevent(kq, 0, 0, &e, 1, 0);
    check(nev >= 0);

    if (e.filter == EVFILT_WRITE) {
      handle_event(u32(usize(e.udata)), WriteD);
    } else if (e.filter == EVFILT_READ) {
      handle_event(u32(usize(e.udata)), ReadD);
    } else {
      abort();
    }
  }
}
