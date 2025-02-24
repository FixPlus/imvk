add_subdirectory(common)

include(../cmake/compile_shaders.cmake)
add_library(SHADER_LIB INTERFACE)
compileShaders(${CMAKE_CURRENT_SOURCE_DIR}/shaders assets/shaders SHADER_LIB assets/shaders)

function(imvk_add_example NAME)
    file(GLOB_RECURSE ${NAME}_SOURCES ${NAME}/*.cpp ${NAME}/*.h ${NAME}/*.hpp)
    add_executable(${NAME} ${${NAME}_SOURCES} ${IMVK_ALL_INCLUDES})
    target_link_libraries(${NAME} PUBLIC imvk_examples_lib SHADER_LIB)
endfunction(imvk_add_example)

imvk_add_example(hello_world)
