cmake_minimum_required(VERSION 3.9)
project(envredis C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -fPIC -Wall -Wextra")

add_library(envredis SHARED library.c)

target_link_libraries(envredis dl hiredis)
