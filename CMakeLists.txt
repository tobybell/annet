cmake_minimum_required(VERSION 3.20)

project(Annet)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_library(Annet STATIC annet-mac.cc print.cc)

add_executable(Server2 server.cc print.cc)
target_link_libraries(Server2 PRIVATE Annet)

add_executable(ChatServer ChatServer.cc)
target_link_libraries(ChatServer PRIVATE Annet)


