cmake_minimum_required(VERSION 3.20)

project(Annet)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_library(Annet STATIC print.cc annet-dns.cc)
target_include_directories(Annet PUBLIC .)

if(MSVC)
  target_sources(Annet PRIVATE annet-windows.cc)
  target_link_libraries(Annet PRIVATE ws2_32)
elseif(APPLE)
  target_sources(Annet PRIVATE annet-mac.cc)
elseif(UNIX)
  target_sources(Annet PRIVATE annet-linux.cc)
endif()

add_executable(EchoServer example/EchoServer.cc)
target_link_libraries(EchoServer PRIVATE Annet)

add_executable(ChatServer example/ChatServer.cc)
target_link_libraries(ChatServer PRIVATE Annet)

add_executable(Client example/Client.cc)
target_link_libraries(Client PRIVATE Annet)
