cmake_minimum_required(VERSION 3.25)

project(imvk)

set(CMAKE_CXX_STANDARD 20)

find_package(vkw 0.0.2 REQUIRED)

add_subdirectory(lib)
set(IMVK_ASSETS_PATH ${CMAKE_CURRENT_SOURCE_DIR}/assets)
cmake_path(ABSOLUTE_PATH IMVK_ASSETS_PATH)
add_subdirectory(examples)
