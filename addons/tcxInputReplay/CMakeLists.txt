cmake_minimum_required(VERSION 3.16)
add_library(tcxInputReplay INTERFACE)
target_include_directories(tcxInputReplay INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(tcxInputReplay INTERFACE TrussC)
