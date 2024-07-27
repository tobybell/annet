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
  u32 server = check_listen(8000);

  List<Client> clients;
  List<u32> free_client;

  Outgoing* tail = new Outgoing {{}, 0, 0};

  void start_accept() {
    an::accept(server, [this](i32 res) {
      if (res < 0)
        return println("accept error");
      auto sock = u32(res);
      ++tail->n_ref;

      u32 id;
      if (free_client) {
        id = free_client.last();
        free_client.pop();
      } else {
        id = len(clients);
        clients.emplace();
      }

      clients[id] = {sock, tail};

      Print p;
      sprint(p, "Client ", id, " joined.\n");
      broadcast(p.chars.take(), id);

      read_client(id);
      start_accept();
    });
  }

  void broadcast(String s, u32 sender) {
    auto old_tail = tail = tail->next = new Outgoing {::move(s), sender, 1};
    // if (--old_tail->n_ref == 0)
    //   delete old_tail;
    for (u32 i {}; i < len(clients); ++i)
      try_send(i);
  }

  void close_client(u32 id) {
    an::close(clients[id].sock);
    clients[id].sock = ~0u;
    free_client.push(id);

    Print p;
    sprint(p, "Client ", id, " left.\n");
    broadcast(p.chars.take(), id);
  }

  void read_client(u32 id) {
    String buf(1024);
    auto s = buf.begin();
    an::read(clients[id].sock, s, 1024, [this, id, buf = move(buf)](i32 n) mutable {
      if (n <= 0)
        return close_client(id);

      buf.data = (char*) realloc(buf.data, n);
      buf.size = n;
      broadcast(move(buf), id);

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
    if (head->sender == client || info.sock == ~0u)
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
