cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include_directories(/opt/homebrew/Cellar/boost/1.78.0_1/include)
project(Server)

add_executable(Server server_side_next.h schedulor.h server.cc)