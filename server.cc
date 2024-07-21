#include "common.hh"
#include "print.hh"

#include "annet.hh"

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
             rec->val(forward<Arg>(arg)...);
             delete rec;
           },
           ::move(t)})
          ->f) {}
};

void an_accept(unsigned s, Callback<int> cb) { async_accept(s, cb.f); }

void an_read(unsigned s, Mut<char> dst, Callback<unsigned> cb) {
  async_read(s, dst.begin(), len(dst), cb.f);
}

void an_write(unsigned s, Str src, Callback<bool> cb) {
  async_write(s, src.begin(), len(src), cb.f);
}

void echo_client(u32 client, String buf) {
  an_read(client, buf, [client, buf = move(buf)](u32 n) {
    if (!n) {
      println("close ", client);
      return an_close(client);
    }
    an_write(client, {buf.begin(), n}, [client, buf = move(buf)](u32 n) {
      echo_client(client, move(buf));
    });
  });
}

void accept_client(u32 server) {
  an_accept(server, [server](i32 sock) {
    if (sock < 0)
      return println("accept error");
    echo_client(unsigned(sock), String(1024u));
    accept_client(server);
  });
}

i32 main() {
  i32 server = listen_tcp(8000);
  check(server >= 0);
  accept_client(u32(server));
  run();
}
