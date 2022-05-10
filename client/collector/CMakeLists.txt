cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)
SET(CMAKE_CXX_FLAGS " -pthread") 

include_directories(/root/boost_1_78_0)
project(Collector)

add_executable(Collector collector_side.h collector.cc)