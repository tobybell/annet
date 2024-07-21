#include "common.hh"
#include "print.hh"

#include "annet.hh"

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


template <class T, class... Arg>
struct Cb {
  void (*f)(void*, Arg...);
  T val;
};

template <class... Arg>
struct CbFactory {
  void (**f)(void*, Arg...);

  template <class T>
  CbFactory(T&& t) {
    auto obj = new Cb<T, Arg...> {[](void* self, Arg... arg) {
      auto cb = (Cb<T, Arg...>*) self;
      cb->val(forward<Arg>(arg)...);
      delete cb;
    }, ::move(t)};
    f = &obj->f;
  }
};

void an_accept(unsigned s, CbFactory<int> cb) {
  async_accept(s, cb.f);
}

void an_read(unsigned s, Mut<char> dst, CbFactory<unsigned> cb) {
  async_read(s, dst.begin(), len(dst), cb.f);
}

void an_write(unsigned s, Str src, CbFactory<bool> cb) {
  async_write(s, src.begin(), len(src), cb.f);
}

using isize = signed long;

i32 firstFd {};

void alternative_connection(i32 fd) {
  char const* data = (char const*) malloc(16 * 1024 * 1024);
  an_write(fd, {data, 16 * 1024 * 1024}, [fd, data](bool err) {
    free((void*) data);
    println("WROTE IT! ", err);
    close(fd);
  });
}

static constexpr u32 ChunkSize = 512 * 1024;

void copy_chunk(i32 fd, i32 dst, char* chunk) {
  println("copy_chunk ", fd, ' ', dst);
  an_read(fd, {chunk, ChunkSize}, [fd, dst, chunk](u32 size) {
    println("DID READ ", size);
    if (!size) {
      println("finishing because read done");
      close(fd);
      close(dst);
      return free((void*) chunk);
    }
    an_write(dst, {chunk, size}, [fd, dst, chunk, size](bool err) {
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

void wait_read(i32 conn, char* buf) {
  an_read(conn, {buf, 1024}, [buf, conn](u32 n) {
    println("$ received ", conn, ' ', n, ' ');
    if (n) {
      wait_read(conn, buf);
    } else {
      free(buf);
    }
  });
}

void handle_accept(i32 conn) {
  // g.joined(client);

  char* buf = (char*) malloc(1024);
  wait_read(conn, buf);
  return;


  if (!firstFd) {
    firstFd = conn;
    // alternative_connection(conn);
  } else {
    second_connection(conn, exchange(firstFd, 0));
  }
}

unsigned check_valid(int r) {
  check(r >= 0);
  return unsigned(r);
}

int main() {
  unsigned server = check_valid(listen_tcp(8089));

  an_accept(server, [](int sock) {
    if (sock < 0)
      return println("accept error");
    println("accepted ", sock);
  });

  run();
}
