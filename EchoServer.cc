#include "common.hh"
#include "print.hh"

#include "annet.hh"

void echo_client(u32 client, String buf) {
  an::read(client, buf.begin(), len(buf), [client, buf = move(buf)](u32 n) {
    if (n <= 0) {
      println("close ", client);
      return an::close(client);
    }
    an::write(client, buf.begin(), n, [client, buf = move(buf)](u32 n) {
      echo_client(client, move(buf));
    });
  });
}

void accept_client(u32 server) {
  an::accept(server, [server](i32 sock) {
    if (sock < 0)
      return println("accept error");
    echo_client(unsigned(sock), String(1024u));
    accept_client(server);
  });
}

i32 main() {
  i32 server = an::listen(8000);
  check(server >= 0);
  accept_client(u32(server));
  an::run();
}
