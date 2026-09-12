add_executable(imvk_graph_pass_tests graph/InsertFormatConversions.cpp)
target_link_libraries(imvk_graph_pass_tests PRIVATE imvk)

add_test(NAME imvk.graph.insert_format_conversions
         COMMAND imvk_graph_pass_tests)