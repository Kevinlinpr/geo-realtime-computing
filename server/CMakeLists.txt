cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)
SET(CMAKE_CXX_FLAGS " -pthread")

include_directories(/root/boost_1_78_0)
project(Server)

add_executable(Server server_side_next.h schedulor.h server.cc)