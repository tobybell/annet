extern "C" {
#include "annet.h"
}

#include "common.hh"
#include "print.hh"

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
 
void set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

template <class T>
void finish(void (**&cb)(void*, T), T arg) {
  auto f = exchange(cb, nullptr);
  (*f)(f, arg);
}

struct Socket {
  void (**r_done)(void*, int n);
  void (**w_done)(void*, bool err);
  char* r_buf;
  char const* w_buf;
  i32 fd;
  u32 r_len;
  u32 w_len;
  bool server;

  void accept(void (**cb)(void*, int sock)) {
    println("accept fd=", fd, " cb=", (void*) cb);
    check(server);
    check(!r_done);
    r_done = cb;
    try_accept();
  }

  void read(char* dst, u32 len, void (**done)(void*, int)) {
    check(!r_done);
    check(len);
    r_buf = dst;
    r_len = len;
    r_done = done;
    try_read();
  }

  void write(char const* src, u32 len, void (**done)(void*, bool)) {
    check(!w_done);
    check(len);
    w_buf = src;
    w_len = len;
    w_done = done;
    try_write();
  }

  void try_accept();

  void try_read() {
    isize n = ::read(fd, r_buf, r_len);
    if (n >= 0) {
      finish(r_done, int(n));
    } else if (errno != EAGAIN) {
      finish(r_done, -1);
    }
  }

  void try_write() {
    check(w_buf);
    isize n = ::write(fd, w_buf, w_len);
    if (w_len <= n) {
      finish(w_done, false);
    } else if (0 < n) {
      w_buf += n;
      w_len -= n;
    } else if (!n || errno != EAGAIN) {
      finish(w_done, true);
    }
  }
};

int ep {};
List<Socket> sockets;
List<u32> free_socket;

u32 alloc_sock() {
  if (free_socket) {
    u32 ans = free_socket.last();
    free_socket.pop();
    return ans;
  } else {
    u32 ans = len(sockets);
    sockets.emplace();
    return ans;
  }
}

void free_sock(u32 id) {
  if (id + 1 == len(sockets)) {
    sockets.pop();
  } else {
    free_socket.push(id);
  }
}

void Socket::try_accept() {
  check(server);
  sockaddr_in cli;
  socklen_t len = sizeof(cli);
  int r = ::accept(fd, (sockaddr*) &cli, &len);
  if (r >= 0) {
    set_nonblocking(r);

    // do this here because we are about to clobber the sockets list
    auto cb = exchange(r_done, nullptr);

    u32 ans = alloc_sock();
    sockets[ans] = {};
    sockets[ans].fd = r;

    epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    ev.data.u32 = ans;
    check(!epoll_ctl(ep, EPOLL_CTL_ADD, r, &ev));

    (*cb)(cb, ans);
  } else if (errno != EWOULDBLOCK) {
    finish(r_done, -1);
  }
}

void an_accept(unsigned server, void (**cb)(void*, int sock)) {
  sockets[server].accept(cb);
}

void an_write(u32 fd, char const* src, u32 len, void (**cb)(void*, bool)) {
  sockets[fd].write(src, len, cb);
}

void an_read(u32 fd, char* dst, u32 len, void (**cb)(void*, int n)) {
  sockets[fd].read(dst, len, cb);
}

int an_listen(u16 port) {
  if (!ep)
    ep = epoll_create(1);

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

  u32 ans = alloc_sock();
  sockets[ans] = Socket {};
  sockets[ans].fd = acceptor;
  sockets[ans].server = true;

  epoll_event ev;
  ev.events = EPOLLIN | EPOLLET;
  ev.data.u32 = ans;
  check(!epoll_ctl(ep, EPOLL_CTL_ADD, acceptor, &ev));

  return ans;
}

void an_close(unsigned id) {
  auto& sock = sockets[id];
  if (sock.server) {
    if (sock.r_done)
      (*sock.r_done)(sock.r_done, -1);
  } else {
    if (sock.r_done)
      (*sock.r_done)(sock.r_done, -1);
    if (sock.w_done)
      (*sock.w_done)(sock.w_done, -1);
  }
  close(sock.fd);
  free_sock(id);
}

void an_run() {
  if (!ep)
    return;
  for (;;) {
    epoll_event e;
    i32 r = epoll_wait(ep, &e, 1, -1);
    check(r >= 0);
    if (!r)
      continue;
    auto id = e.data.u32;
    auto& sock = sockets[id];
    if (sock.server) {
      println("got server event id=", id);
      if (e.events & EPOLLIN) {
        if (sock.r_done)
          sock.try_accept();
      } else {
        println("unknown server event");
      }
    } else {
      println("got client event id=", id);
      if (e.events & EPOLLIN) {
        if (sock.r_done)
          sock.try_read();
      }
      if (e.events & EPOLLOUT) {
        if (sock.w_done)
          sock.try_write();
      }
    }
  }
}
