cmake_minimum_required(VERSION 3.10)
project(Concurrency)

set(CMAKE_CXX_STANDARD 17)

find_package(Threads)
add_executable(Concurrency main.cpp)
target_link_libraries(Concurrency ${CMAKE_THREAD_LIBS_INIT})