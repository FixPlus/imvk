file(GLOB_RECURSE IMVK_ALL_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../include/imvk/*.h ${CMAKE_CURRENT_SOURCE_DIR}/../include/imvk/*.hpp)

set(VK_LIB VkWrapper::VkWrapper)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  if(MSVC)
    set(VK_LIB VkWrapper::VkWrapperd)
  endif()
endif()

function(imvk_add_component NAME)
  file(GLOB_RECURSE ${NAME}_SOURCES ${NAME}/*.cpp ${NAME}/*.h ${NAME}/*.hpp)
  add_library(imvk_${NAME} STATIC ${${NAME}_SOURCES} ${IMVK_ALL_INCLUDES})
  target_include_directories(imvk_${NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include)
  target_link_libraries(imvk_${NAME} PUBLIC ${VK_LIB})
endfunction()

set(IMVK_COMPONENTS base graphics)

foreach(COMPONENT ${IMVK_COMPONENTS})
  imvk_add_component(${COMPONENT})
endforeach()

add_library(imvk INTERFACE)

foreach(COMPONENT ${IMVK_COMPONENTS})
  target_link_libraries(imvk INTERFACE imvk_${COMPONENT})
endforeach()
