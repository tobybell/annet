# Annet

Simple, cross-platform async networking.

## Alternative design

An alternative to function pointers would be to go for a truly event-based
design, in which the only messages to and from the library relate to socket
numbers. Basically like file descriptors.

```
  <- write(sock u32, len u32, data[len] char)
  <- read(sock u32, len u32, data[len] char mut)

  -> did_write(sock u32, bool err)
  -> did_read(sock u32, u32 len)

  <- listen(port u16) -> server u32

  <- accept(server u32)
  -> did_accept(server u32, sock u32)

  <- connect(ip u32, port u16) -> connection u32
  -> did_connect(connection u32, sock u32)
```

## License

This software is [unlicensed](LICENSE). You may not use, redistribute, or
create derivative works from this software without my permission.
