cmake_minimum_required(VERSION 3.22)
project(kq_tunnel VERSION 0.1 LANGUAGES CXX)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(asio REQUIRED)
find_package(spdlog REQUIRED)

add_subdirectory(src/common)
add_subdirectory(src/plugin)
add_subdirectory(src/client)
add_subdirectory(src/server)
