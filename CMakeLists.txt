cmake_minimum_required(VERSION 3.25)

project(imvk)

set(CMAKE_CXX_STANDARD 20)

find_package(VkWrapper 3 REQUIRED)

add_subdirectory(lib)
add_subdirectory(examples)
