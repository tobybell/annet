#include "annet.hh"

#include "common.hh"
#include "print.hh"

constexpr char const resp[] =
  "HTTP/1.1 200 OK\r\n"
  "Connection: close\r\n"
  "\r\n"
  "<b style=\"color:red\">Hello, world!</b>\n";

u32 check_listen(u16 port) {
  i32 r = an::listen(port);
  check(r >= 0);
  return u32(r);
}

void consume_rest(u32 sock) {
  static char buf[1024];
  an::read(sock, buf, 1024, [sock](i32 n) {
    if (n <= 0) {
      an::close(sock);
      println("closed client");
    } else {
      consume_rest(sock);
    }
  });
}

void accept_client(u32 server) {
  an::accept(server, [server](i32 res) {
    if (res < 0)
      return println("accept error");
    auto sock = u32(res);
    println("accepted client");
    an::write(sock, resp, sizeof(resp) - 1, [sock](i32 n) {
      an::write_done(sock);
      consume_rest(sock);
    });
    accept_client(server);
  });
}

i32 main() {
  u32 server = check_listen(80);
  accept_client(server);
  an::run();
}
