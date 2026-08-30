add_library(imvk_imgui STATIC imgui/imgui.cpp imgui/imgui_draw.cpp imgui/imgui_widgets.cpp imgui/imgui_tables.cpp imgui/backends/imgui_impl_glfw.cpp imgui/imgui_demo.cpp)
target_include_directories(imvk_imgui PUBLIC imgui)
target_link_libraries(imvk_imgui PRIVATE glfw)