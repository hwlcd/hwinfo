cmake_minimum_required(VERSION 4.0.0)
project(hwinfo CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_subdirectory(src/hwinfo)
add_subdirectory(src/cli)
