#include <annet.hh>

#include <print.hh>

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
  unsigned ip = an::resolve("google.com");
  if (!ip)
    return println("couldn't resolve"), 0;
  an::connect(ip, 80, [](int r) {
    if (r < 0)
      return println("couldn't connect");
    write_request(r);
  });
  an::run();
}
