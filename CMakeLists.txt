cmake_minimum_required(VERSION 3.10)
project(traffic_generator)

set(CMAKE_CXX_STANDARD 11)

add_executable(traffic_generator src/traffic.c src/traffic.h src/const.h src/util/thread_pool.h src/util/thread_pool.c src/const.c)