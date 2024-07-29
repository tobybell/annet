#include <annet.hh>

#include <stdio.h>
#include <stdlib.h>

void read_response(unsigned sock, char* buf) {
  an::read(sock, buf, 1024, [sock, buf](int n) {
    if (n <= 0)
      return free(buf), an::close(sock);
    printf("%.*s", n, buf);
    read_response(sock, buf);
  });
}

void write_request(unsigned sock) {
  char req[] = "GET / HTTP/1.1\r\nConnection: close\r\n\r\n";
  an::write(sock, req, sizeof(req) - 1, [sock](bool err) {
    if (err)
      return an::close(sock);
    read_response(sock, (char*) malloc(1024));
  });
}

int main() {
  an::init();
  unsigned ip = an::resolve("google.com");
  if (!ip)
    return printf("couldn't resolve"), 0;
  an::connect(ip, 80, [](int r) {
    printf("got connect! r=%d\n", r);
    if (r < 0)
      return (void) printf("couldn't connect");
    write_request(r);
  });
  an::run();
}
