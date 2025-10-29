cmake_minimum_required(VERSION 3.25)

project(imvk)

set(CMAKE_CXX_STANDARD 20)

find_package(vkw 0.0.2 REQUIRED)

add_subdirectory(lib)
add_subdirectory(examples)
