set(CMAKE_C_COMPILER "clang")
set(CMAKE_CXX_COMPILER "clang++")

cmake_minimum_required(VERSION 3.14)
project(a11)

set(CMAKE_C_STANDARD 99)

include_directories(include)

add_executable(server src/server.c)
add_executable(client src/client.c)

