find_package(glfw3 3.3 REQUIRED)

find_package(glm CONFIG REQUIRED)

add_library(imvk_examples_lib STATIC IMVKBuffers.cpp IMVKWindow.cpp IMVKWindow.hpp IMVKDevice.cpp IMVKDevice.hpp
  IMVKShaderLoader.hpp IMVKShaderLoader.cpp IMVKPipeline.hpp IMVKPipeline.cpp IMVKTexture.hpp IMVKTexture.cpp)

target_link_libraries(imvk_examples_lib PUBLIC glm::glm-header-only)
target_compile_definitions(imvk_examples_lib PRIVATE IMVK_ASSETS_PATH=\"${IMVK_ASSETS_PATH}\")
target_link_libraries(imvk_examples_lib PUBLIC glfw imvk)
target_include_directories(imvk_examples_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})