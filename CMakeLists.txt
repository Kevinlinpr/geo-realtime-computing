cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)

project(georealtime)

add_subdirectory(client/collector)
add_subdirectory(client/cuda)
add_subdirectory(server)