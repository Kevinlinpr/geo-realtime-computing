cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include_directories(/opt/homebrew/Cellar/boost/1.78.0_1/include)
project(Collector)

add_executable(Collector collector_side_next.h collector.cc)