extern "C" {
#include "annet.h"
}

#include "msvc.hh"
#include "print.hh"

using u32 = unsigned;
using i32 = int;
using u16 = unsigned short;

#include <ws2tcpip.h>

void write_cerr(Str str) {
  static HANDLE h_stderr = GetStdHandle(STD_ERROR_HANDLE);
  WriteFile(h_stderr, str.base, str.size, 0, 0);
}

void an_init() {
  WSADATA wsa_data;
  check(!WSAStartup(MAKEWORD(2, 2), &wsa_data));
}

void set_nonblocking(SOCKET fd) {
  u_long mode = 1;
  ioctlsocket(fd, FIONBIO, &mode);
}

void an_connect(u32 ip, u16 port, void (**cb)(void*, i32 sock)) {
  println("an_connect ip=", ip, " port=", port, " cb=", (void*) cb);

  SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock == INVALID_SOCKET)
    return (*cb)(cb, -1);

  set_nonblocking(sock);
 
  sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(ip);
  addr.sin_port = htons(port);
  auto r = ::connect(sock, (sockaddr*) &addr, sizeof(addr));
  println("::connect r=", r, " errno=", WSAGetLastError());
  if (r >= 0)
    println("success connect");
  else if (WSAGetLastError() == WSAEWOULDBLOCK) {
    println("got would block");
  }
}

void an_close(u32 sock) {
    println("close sock=", sock);
}

void an_read(u32 sock, char* buf, u32 len, void (**cb)(void*, i32 n)) {
    
}

void an_write(u32 sock, char const* buf, u32 len, void (**cb)(void*, bool n)) {
    
}

void an_run() {

}
