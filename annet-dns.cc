extern "C" {
#include "annet.h"
}

#include <stdio.h>
//#include <ws2tcpip.h>
#include <netdb.h>

unsigned an_resolve(char const* domain) {
  struct addrinfo hint {}, *result;
  hint.ai_family = PF_UNSPEC;
  hint.ai_socktype = SOCK_STREAM;

  if (getaddrinfo(domain, 0, &hint, &result))
    return 0;

  unsigned ans {};
  for (auto i = result; i; i = i->ai_next) {
    if (i->ai_family != AF_INET)
      continue;
    ans = ntohl(((struct sockaddr_in *) i->ai_addr)->sin_addr.s_addr);
    break;
  }

  freeaddrinfo(result);
  return ans;
}
