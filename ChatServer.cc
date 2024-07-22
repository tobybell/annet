#include "common.hh"
#include "print.hh"

#include "annet.hh"

struct Outgoing {
  String message;
  u32 sender;
  u32 n_ref;
  Outgoing* next {};
};

struct Client {
  u32 sock;
  Outgoing* head;
  bool sending {};
};

u32 check_listen(u16 port) {
  i32 r = an::listen(port);
  check(r >= 0);
  return u32(r);
}

struct Server {
  u32 server = check_listen(8001);

  List<Client> clients;
  Outgoing* tail = new Outgoing {};

  void start_accept() {
    an::accept(server, [this](i32 res) {
      if (res < 0)
        return println("accept error");
      auto sock = u32(res);
      ++tail->n_ref;
      u32 id = len(clients);
      clients.push({sock, tail});
      read_client(id);
      start_accept();
    });
  }

  void read_client(u32 id) {
    String buf(1024);
    auto s = buf.begin();
    an::read(clients[id].sock, s, 1024, [this, id, buf = move(buf)](u32 n) mutable {
      if (!n)
        return;  // no more to read

      buf.data = (char*) realloc(buf.data, n);
      buf.size = n;
      tail = tail->next = new Outgoing {move(buf), clients[id].sock, 1};

      for (u32 i {}; i < len(clients); ++i)
        try_send(i);

      read_client(id);
    });
  }

  void try_send(u32 client) {
    auto& info = clients[client];
    if (info.sending)
      return;
    auto& head = info.head;
    auto next = head->next;
    if (!next)
      return;
    if (--head->n_ref == 0) {
      --next->n_ref;
      delete head;
    }
    head = next;
    ++head->n_ref;
    if (head->sender == info.sock)
      return try_send(client);
    auto& msg = head->message;
    info.sending = true;
    an::write(info.sock, msg.begin(), len(msg), [this, client](bool err) {
      clients[client].sending = false;
      if (err)
        return;
      try_send(client);
    });
  }
};

i32 main() {
  Server s;
  s.start_accept();
  an::run();
}
