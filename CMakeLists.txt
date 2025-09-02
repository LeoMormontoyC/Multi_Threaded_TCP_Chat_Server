cmake_minimum_required(VERSION 3.14)
project(a11)

set(CMAKE_C_STANDARD 99)

include_directories(include)

add_executable(server src/server.c)
add_executable(client src/client.c)

