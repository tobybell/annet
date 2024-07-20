#include "common.hh"
#include "print.hh"

#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/event.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <list>

#define MAX 500
#define PORT 8089

bool running {true};
int kq;

using isize = signed long;

// using file_descriptor = int;

// using std::cerr;
// using std::ostream;
// using std::endl;
// using std::forward;
// using std::string;

// struct chunk {
//   Str data;
//   bool eof;  // whether this is last chunk
// };



// struct connection {
//   file_descriptor f_;

//   connection(file_descriptor f): f_(f) {
//     printf("Create [%d].\n", f_);
//   }
//   ~connection() {
//     printf("Destroy [%d].\n", f_);
//   }
//   void read(Str b) {
//     chunk c = {b.begin(), b.end(), false};
//     cerr << str(c) << endl;
//     auto r = parser(c);
//     if (succeeded(r)) {
//       printf("[%d]: Yay! Good parsing!", f_);
//     } else if (failed(r)) {
//       printf("[%d]: Error! Bad parsing!", f_);
//     }
//   }
// };

// void write(u32 client, Str s);

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

// Function designed for chat between client and server.
// void func(int sockfd) {
//   char buff[MAX];
//   int n;
//   // infinite loop for chat
//   for (;;) {
//     bzero(buff, MAX);

//     // read the message from client and copy it in buffer
//     kevent(kq, )
//     read(sockfd, buff, sizeof(buff));
//     // print buffer which contains the client contents
//     printf("From client: %s\t To client : ", buff);
//     bzero(buff, MAX);
//     n = 0;
//     // copy server message in the buffer
//     while ((buff[n++] = getchar()) != '\n')
//       ;

//     // and send that buffer to client
//     write(sockfd, buff, sizeof(buff));

//     // if msg contains "Exit" then server exit and chat ended.
//     if (strncmp("exit", buff, 4) == 0) {
//       printf("Server Exit...\n");
//       break;
//     }
//   }
// }

void makeFileDescriptorNonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

using kernel_event = struct kevent;
using kernel_event_handler = void (*)(kernel_event&);

void add_event(int fd, i16 filter, kernel_event_handler f) {
  struct kevent set {
    .ident = uintptr_t(fd),
    .filter = filter,
    .flags = EV_ADD,
    .udata = reinterpret_cast<void*>(f),
  };
  timespec ts {};
  kevent(kq, &set, 1, 0, 0, &ts);
}

void add_read_event(int fd, kernel_event_handler f) {
  add_event(fd, EVFILT_READ, f);
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
  void (*call)(void*, T...);
  void* obj;
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
  Str data;
  Promise<bool> on_finish;
};

struct PendingRead {
  Mut<char> data;
  Promise<u32> finish;
};

void try_write(i32 fd, PendingWrite* pend) {
  Str s = pend->data;
  isize write = ::write(fd, s.begin(), len(s));
  auto& on_finish = pend->on_finish;
  if (write < 0) {
    if (errno == EAGAIN) {
      println("write return EAGAIN");
      write = 0;
    } else {
      println("write returning negative, errno=", errno);
      on_finish.call(on_finish.obj, true);
      free(pend);
      return;
    }
  } else {
    println("successfully wrote ", write);
    if (len(s) <= write) {
      on_finish.call(on_finish.obj, false);
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
  auto& finish = pend->finish;
  if (ret < 0) {
    if (errno == EAGAIN) {
      // pass through, wait for more input
    } else {
      println("::read returned negative");
      finish.call(finish.obj, 0);
      free(pend);
      return;
    }
  } else if (0 < ret) {
    finish.call(finish.obj, u32(ret));
    free(pend);
    return;
  } else {  // 0 means EOF
    finish.call(finish.obj, 0);
    free(pend);
    return;
  }

  struct kevent set {uintptr_t(fd), EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, reinterpret_cast<void*>(pend)};
  timespec ts {};
  kevent(kq, &set, 1, 0, 0, &ts);
}

void handle_write(kernel_event& e) {
  println("handle_write ", e.ident, ' ', e.data);
  i32 fd = i32(e.ident);
  auto pending = reinterpret_cast<PendingWrite*>(e.udata);
  try_write(fd, pending);
}

void handle_read(kernel_event& e) {
  println("handle_read ", e.ident, ' ', e.data);
  i32 fd = i32(e.ident);
  auto pend = reinterpret_cast<PendingRead*>(e.udata);
  try_read(fd, pend);
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

void async_write(i32 fd, Str s, Promise<bool> on_finish) {
  try_write(fd, new PendingWrite {s, ::move(on_finish)});
}

void async_read(i32 fd, Mut<char> s, Promise<u32> finish) {
  try_read(fd, new PendingRead {s, ::move(finish)});
}

i32 firstFd {};

void alternative_connection(i32 fd) {
  char const* data = (char const*) malloc(16 * 1024 * 1024);
  async_write(fd, {data, 16 * 1024 * 1024}, [fd, data](bool error) {
    free((void*) data);
    println("WROTE IT! ", error);
    close(fd);
  });
}

static constexpr u32 ChunkSize = 512 * 1024;

void copy_chunk(i32 fd, i32 dst, char* chunk) {
  println("copy_chunk ", fd, ' ', dst);
  async_read(fd, {chunk, ChunkSize}, [fd, dst, chunk](u32 size) {
    println("DID READ ", size);
    if (!size) {
      println("finishing because read done");
      close(fd);
      close(dst);
      return free((void*) chunk);
    }
    async_write(dst, {chunk, size}, [fd, dst, chunk](bool err) {
      if (err) {
        println("finishing because write done");
        close(fd);
        close(dst);
        return free((void*) chunk);
      }
      println("wrote it!");
      copy_chunk(fd, dst, chunk);
    });
  });
}

void second_connection(i32 fd, i32 dst) {
  println("second_connection ", fd, ' ', dst);
  char* data = (char*) malloc(ChunkSize);
  copy_chunk(fd, dst, data);
}

void handle_accept(kernel_event& e) {
  i32 f = i32(e.ident);
  sockaddr_in cli;
  socklen_t len = sizeof(cli);
  i32 conn = accept(f, (sockaddr*) &cli, &len);
  check(conn >= 0);

  makeFileDescriptorNonblocking(conn);

  u32 client = allocateClient(u32(conn));
  setClient(u32(conn), client);

  // add_read_event(conn, handle_read);

  // g.joined(client);

  if (!firstFd) {
    firstFd = conn;
    // alternative_connection(conn);
  } else {
    second_connection(conn, exchange(firstFd, 0));
  }
}

void handle_input(kernel_event& e) {
  i32 f = i32(e.ident);
  if (e.flags & EV_EOF) {
    close(f);
  } else {
    char buf[MAX];
    isize n = read(f, buf, sizeof(buf));
    (void) n;
    // for (auto& c: conns) {
    //   write(c.f_, buf, n);
    // }
  }
}

void handle_sigint(int sig) {
  (void) sig;
  running = false;
}

int main() {
  kq = kqueue();

  sockaddr_in servaddr;

  int acceptor = socket(AF_INET, SOCK_STREAM, 0);
  check(acceptor >= 0);
  makeFileDescriptorNonblocking(acceptor);

  // assign IP, PORT
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  servaddr.sin_port = htons(PORT);

  // Binding newly created socket to given IP and verification
  if ((bind(acceptor, (sockaddr*)&servaddr, sizeof(servaddr))) != 0) {
    printf("Bind socket failed...\n");
    exit(1);
  } else {
    printf("Bound socket.\n");
  }

  // Now server is ready to listen and verification
  if ((listen(acceptor, 5)) != 0) {
    printf("Listen failed...\n");
    exit(0);
  } else {
    printf("Server listening on %d...\n", PORT);
  }

  add_read_event(acceptor, handle_accept);

  makeFileDescriptorNonblocking(STDIN_FILENO);
  add_read_event(STDIN_FILENO, handle_input);

  signal(SIGINT, handle_sigint);

  constexpr auto timeout_s = 5;

  while (running) {
    struct kevent mon;
    timespec ts {timeout_s, 0};
    int nev = kevent(kq, 0, 0, &mon, 1, &ts);
    if (nev < 1) {
      if (running) {
        printf("No events for last %d seconds.\n", timeout_s);
      }
      continue;
    }
    auto f = reinterpret_cast<kernel_event_handler>(mon.udata);
    if (mon.filter == EVFILT_WRITE) {
      handle_write(mon);
    } else if (mon.filter == EVFILT_READ && i32(mon.ident) != acceptor) {
      handle_read(mon);
    } else {
      f(mon);
    }
  }
  println("exiting after interrupt");

  close(acceptor);

  return 0;
}
