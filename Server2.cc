#include "common.hh"
#include "print.hh"

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX 500
#define PORT 8080

bool running {true};
i32 ep;

using isize = signed long;

struct Server {

  void joined(u32 client) {
    println("joined client=", client);
    // write(client, "Enter your name: "_s);
  }

  void read(u32 client, Str a) {
    println("read client=", client, " '", a, "'");
  }

  void close(u32 client) {
    println("closed client=", client);
  }
};

Server g;

void makeFileDescriptorNonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

u32 clientFreeList;
List<u32> clientToFileDescriptor;
List<u32> fileDescriptorToClient;

void write(u32 client, Str s) {
  u32 fd = clientToFileDescriptor[client];
  isize write = ::write(i32(fd), s.begin(), len(s));
  check(len(s) <= write);
}

template <class... T>
struct Promise {
  void (*call)(void*, T...) {};
  void* obj {};
  Promise() = default;

  template <
      class F,
      enable_if<
          is_invocable<F, T...> && is_move_constructible<F> &&
          (sizeof(obj) < sizeof(F) || !is_trivially_copyable<F>)> = 0>
  Promise(F&& f):
    call([](void* arg, T... args) {
      auto& fn = *reinterpret_cast<F*>(arg);
      fn(forward<T>(args)...);
      fn.~F();
      free(arg);
    }),
    obj(malloc(sizeof(F))) {
    new (obj) F(move(f));
  }
  template <
      class F,
      enable_if<
          is_invocable<F, T...> && is_move_constructible<F> &&
          sizeof(F) <= sizeof(obj) && is_trivially_copyable<F>> = 0>
  Promise(F&& f):
    call([](void* arg, T... args) {
      auto& fn = *reinterpret_cast<F*>(&arg);
      fn(forward<T>(args)...);
      fn.~F();
    }),
    obj() {
    new (&obj) F(move(f));
  }
  Promise(Promise const&) = delete;
  Promise(Promise&& rhs): call(rhs.call), obj(rhs.obj) {
    rhs.call = 0;
    rhs.obj = 0;
  }
};

struct PendingWrite {
  i32 fd;
  Str data;
  Promise<bool> on_finish;
};

struct PendingRead {
  i32 fd;
  Mut<char> data;
  Promise<u32> finish;
};

bool try_write(PendingWrite* pend) {
  Str s = pend->data;
  isize write = ::write(pend->fd, s.begin(), len(s));
  println("::write ", pend->fd, ' ', write);
  auto& finish = pend->on_finish;
  if (write < 0) {
    if (errno == EAGAIN)
      return false;
    finish.call(finish.obj, true);  // TODO: communicate error to user?
    free(pend);
    return true;
  }
  if (len(s) <= write) {
    finish.call(finish.obj, false);
    free(pend);
    return true;
  }
  pend->data.base += u32(write);
  pend->data.size -= u32(write);
  return false;
}

bool try_read(PendingRead* pend) {
  isize n = read(pend->fd, pend->data.begin(), len(pend->data));
  println("::read fd=", pend->fd, " len=", len(pend->data), " ans=", n, " errno=", errno);
  auto& finish = pend->finish;
  if (n < 0 && errno != EAGAIN) {
    finish.call(finish.obj, 0);  // TODO: communicate error to user?
    free(pend);
    return true;
  }
  if (n >= 0) {
    finish.call(finish.obj, u32(n));
    free(pend);
    return true;
  }
  return false;
}

u32 allocateClient(u32 fileDescriptor) {
  if (!clientFreeList) {
    u32 newClientId = len(clientToFileDescriptor);
    clientToFileDescriptor.push(fileDescriptor);
    return newClientId;
  }

  u32 newClientId = clientFreeList - 1;
  clientFreeList = clientToFileDescriptor[newClientId];
  clientToFileDescriptor[newClientId] = fileDescriptor;
  return newClientId;
}

void setClient(u32 fileDescriptor, u32 client) {
  if (len(fileDescriptorToClient) <= fileDescriptor)
    fileDescriptorToClient.expand(fileDescriptor + 1);
  fileDescriptorToClient[fileDescriptor] = client;
}

struct Socket {
  i32 read_fd;
  i32 write_fd;
};

u32 socketFreeList = 0;
List<Socket> sockets;
List<u32> fdSocket;

void async_write(u32 sock, Str s, Promise<bool> on_finish) {
  i32 fd = sockets[sock].write_fd;
  println("async_write sock=", sock, " len=", len(s), " fd=", fd);
  auto pend = new PendingWrite {fd, s, ::move(on_finish)};
  if (!try_write(pend)) {
    epoll_event ev;
    ev.events = EPOLLOUT | EPOLLHUP | EPOLLRDHUP | EPOLLONESHOT;
    ev.data.ptr = pend;
    check(!epoll_ctl(ep, EPOLL_CTL_MOD, fd, &ev));
  }
}

void async_read(u32 sock, Mut<char> s, Promise<u32> finish) {
  i32 fd = sockets[sock].read_fd;
  check(!!s);
  auto pend = new PendingRead {fd, s, ::move(finish)};
  if (!try_read(pend)) {
    epoll_event ev;
    ev.events = EPOLLIN | EPOLLHUP | EPOLLRDHUP | EPOLLONESHOT;
    ev.data.ptr = pend;
    check(!epoll_ctl(ep, EPOLL_CTL_MOD, fd, &ev));
  }
}

void close_write(u32 sock) {
  i32 fd = sockets[sock].write_fd;
  println("close_write sock=", sock, " fd=", fd);
  shutdown(fd, SHUT_WR);
}

void close_read(u32 sock) {
  i32 fd = sockets[sock].read_fd;
  println("close_read sock=", sock, " fd=", fd);
  shutdown(fd, SHUT_RD);
}

MaybeU32 firstSock {};

//void alternative_connection(i32 fd) {
//  char const* data = (char const*) malloc(16 * 1024 * 1024);
//  async_write(fd, {data, 16 * 1024 * 1024}, [fd, data](bool error) {
//    free((void*) data);
//    println("WROTE IT! ", error);
//    close(fd);
//  });
//}

static constexpr u32 ChunkSize = 512 * 1024;

void copy_chunk(u32 fd, u32 dst, char* chunk) {
  println("copy_chunk ", fd, ' ', dst);
  async_read(fd, {chunk, ChunkSize}, [fd, dst, chunk](u32 size) {
    println("DID READ ", size);
    if (!size) {
      println("finishing because read done");
      close_write(dst);
      return free((void*) chunk);
    }
    async_write(dst, {chunk, size}, [fd, dst, chunk](bool err) {
      if (err) {
        println("finishing because write done");
        return free((void*) chunk);
      }
      println("wrote it!");
      copy_chunk(fd, dst, chunk);
    });
  });
}

void second_connection(u32 fd, u32 dst) {
  println("second_connection ", fd, ' ', dst);
  close_write(fd);
  close_read(dst);
  char* data = (char*) malloc(ChunkSize);
  copy_chunk(fd, dst, data);
}

void handle_accept(i32 s) {
  if (s < 0)
    return println("accept error");
  u32 sock = u32(s);

  // println("handled accept ", sock);
  // Array<char> buf(1024);
  // Mut<char> dst = buf;
  // async_read(sock, dst, [sock, buf = move(buf)](u32 n_read) {
  //   println("received '", Str {buf.begin(), n_read}, '\'');
  //   async_write(sock, "Received!"_s, [](bool err) {
  //     println("finished write err=", err);
  //   });
  // });

  // return;

  if (!firstSock) {
    firstSock = sock;
  } else {
    second_connection(sock, *exchange(firstSock, none));
  }
}

void handle_sigint(int sig) {
  (void) sig;
  running = false;
}

static void epoll_ctl_add(i32 ep, i32 fd, u32 events) {
  epoll_event ev;
  ev.events = events;
  ev.data.fd = fd;
  check(!epoll_ctl(ep, EPOLL_CTL_ADD, fd, &ev));
}

u32 allocateSocket(i32 read_fd, i32 write_fd) {
  if (!socketFreeList) {
    u32 ans = len(sockets);
    sockets.push({read_fd, write_fd});
    return ans;
  }
  u32 ans = socketFreeList - 1;
  socketFreeList = u32(sockets[ans].read_fd);
  sockets[ans] = {read_fd, write_fd};
  return ans;
}

u32 makeSocket(i32 read_fd, i32 write_fd) {
  u32 socket = allocateSocket(read_fd, write_fd);
  u32 max = u32(read_fd < write_fd ? write_fd : read_fd);
  if (len(fdSocket) <= max) {
    fdSocket.expand(max + 1);
    fdSocket.size = max + 1;
  }
  fdSocket[u32(read_fd)] = fdSocket[u32(write_fd)] = socket + 1;
  return socket;
}

void freeSocket(u32 sock) {
  auto& fds = sockets[sock];
  check(!epoll_ctl(ep, EPOLL_CTL_DEL, fds.read_fd, 0));
  check(!epoll_ctl(ep, EPOLL_CTL_DEL, fds.write_fd, 0));
  close(fds.read_fd);
  close(fds.write_fd);

  fdSocket[u32(fds.read_fd)] = 0;
  fdSocket[u32(fds.write_fd)] = 0;
  fds.read_fd = i32(socketFreeList);
  socketFreeList = sock + 1;
}

int main() {
  sockaddr_in servaddr {};
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = htonl(INADDR_ANY);

  i32 acceptor = socket(AF_INET, SOCK_STREAM, 0);
  check(acceptor >= 0);
  makeFileDescriptorNonblocking(acceptor);

  // Binding newly created socket to given IP and verification
  for (u16 i = PORT; i < PORT + 10; ++i) {
    servaddr.sin_port = htons(i);
    if (!bind(acceptor, (sockaddr*) &servaddr, sizeof(servaddr))) {
      println("bound port=", i);
      break;
    }
  }

  // Now server is ready to listen and verification
  if ((listen(acceptor, 5)) != 0) {
    printf("Listen failed...\n");
    exit(0);
  } else {
    printf("Server listening on %d...\n", PORT);
  }

  ep = epoll_create(1);
  epoll_ctl_add(ep, acceptor, EPOLLIN | EPOLLOUT | EPOLLET);

  signal(SIGINT, handle_sigint);

  while (running) {
    static constexpr u32 MAX_EVENTS = 10;
    epoll_event events[MAX_EVENTS];
    i32 ret = epoll_wait(ep, events, MAX_EVENTS, -1);
    check(ret >= 0);
    u32 nfds = u32(ret);
    for (u32 i {}; i < nfds; ++i) {
      if (events[i].data.fd == acceptor) {
        sockaddr_in cli_addr;
        u32 socklen = sizeof(cli_addr);
        i32 conn = accept(acceptor, (sockaddr*) &cli_addr, &socklen);
        if (conn < 0)
          handle_accept(-1);
        makeFileDescriptorNonblocking(conn);
        i32 conn_w = dup(conn);
        //println("shutdown write fd=", conn);
        //shutdown(conn, SHUT_WR);
        //println("shutdown read fd=", conn_w);
        //shutdown(conn_w, SHUT_RD);
        epoll_event ev {};
        ev.events = EPOLLHUP | EPOLLRDHUP | EPOLLONESHOT;
        ev.data.ptr = new PendingRead {conn, {}, {}};
        check(!epoll_ctl(ep, EPOLL_CTL_ADD, conn, &ev));
        ev.data.ptr = new PendingWrite {conn_w, {}, {}};
        check(!epoll_ctl(ep, EPOLL_CTL_ADD, conn_w, &ev));
        u32 socket = makeSocket(conn, conn_w);
        println("connected sock=", socket, " rfd=", conn, " wfd=", conn_w);
        handle_accept(i32(socket));
        continue;
      }

      auto evs = events[i].events;
      if (evs & (EPOLLRDHUP | EPOLLHUP)) {
        println("hangup pend=", (void*) events[i].data.ptr, " RDHUP=", !!(evs & EPOLLRDHUP), " HUP=", !!(evs & EPOLLHUP));
        auto pend = (PendingRead*) events[i].data.ptr;
        auto fd = pend->fd;
        println("hangup RDHUP=", !!(evs & EPOLLRDHUP), " HUP=", !!(evs & EPOLLHUP), " fd=", fd);
        u32 socket = fdSocket[u32(fd)];
        if (!socket--) {
          println("stale fd");
          continue;
        }
        println("socket=", socket);
        freeSocket(socket);
        continue;
      }

      if (events[i].events & EPOLLIN) {
        auto read = (PendingRead*) events[i].data.ptr;
        i32 fd = read->fd;
        println("event=EPOLLIN fd=", fd);
        if (!try_read(read)) {
          println("epoll_ctl MOD fd=", fd);
          check(!epoll_ctl(ep, EPOLL_CTL_MOD, fd, 0));
        }
      } else if (events[i].events & EPOLLOUT) {
        auto write = (PendingWrite*) events[i].data.ptr;
        i32 fd = write->fd;
        println("event=EPOLLOUT fd=", write->fd);
        if (!try_write(write)) {
          println("epoll_ctl MOD fd=", fd);
          epoll_ctl(ep, EPOLL_CTL_MOD, fd, 0);
        }
      } else {
        println("unexpected");
      }
    }
  }

  println("exiting after interrupt");

  close(acceptor);

  return 0;
}
