#include "annet.hh"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

using u32 = unsigned;
using i32 = int;

void echo_client(u32 client, char* buf) {
  an::read(client, buf, 1024, [client, buf](u32 n) {
    if (n <= 0) {
      free(buf);
      printf("close %u\n", client);
      return an::close(client);
    }
    an::write(client, buf, n, [client, buf](u32 n) {
      echo_client(client, buf);
    });
  });
}

void accept_client(u32 server) {
  an::accept(server, [server](i32 sock) {
    if (sock < 0)
      return (void) printf("accept error\n");
    echo_client(u32(sock), (char*) malloc(1024));
    accept_client(server);
  });
}

i32 main() {
  i32 server = an::listen(8000);
  assert(server >= 0);
  accept_client(u32(server));
  an::run();
}
