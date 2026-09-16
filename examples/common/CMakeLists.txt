
add_library(imvk_examples_lib
  STATIC
  IMVKBuffers.cpp
  IMVKWindow.cpp
  IMVKGraphEditor.cpp
  IMVKDevice.cpp
  IMVKModel.cpp
  IMVKShaderLoader.cpp
  IMVKPipeline.cpp
  IMVKTexture.cpp
  IMVKgui.cpp)

target_link_libraries(imvk_examples_lib PUBLIC imvk_tinygltf imvk_imgui imvk_imgui_ne glm::glm-header-only glfw imvk)
target_compile_definitions(imvk_examples_lib PRIVATE IMVK_ASSETS_PATH=\"${IMVK_ASSETS_PATH}\")
target_include_directories(imvk_examples_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})