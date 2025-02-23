find_package(glfw3 3.3 REQUIRED)

add_library(imvk_examples_lib STATIC IMVKWindow.cpp IMVKWindow.hpp IMVKDevice.cpp IMVKDevice.hpp
    IMVKShaderLoader.hpp IMVKShaderLoader.cpp IMVKBasicRenderPass.hpp IMVKBasicRenderPass.cpp)

target_link_libraries(imvk_examples_lib PUBLIC glfw imvk)
target_include_directories(imvk_examples_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})