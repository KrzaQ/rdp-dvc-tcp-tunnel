cmake_minimum_required(VERSION 3.22)
project(kq_tunnel VERSION 0.1 LANGUAGES CXX)

find_package(asio REQUIRED)
find_package(spdlog REQUIRED)

add_subdirectory(src/common)
add_subdirectory(src/plugin)
add_subdirectory(src/client)
add_subdirectory(src/server)
