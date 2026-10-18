cmake_minimum_required(VERSION 3.20)
project(grabCost LANGUAGES C CXX)
include(${TRUSSC_DIR}/cmake/trussc_app.cmake)
trussc_app(NAME grabCost)
if(COST_INTERNAL)
  target_compile_definitions(grabCost PRIVATE TC_COST_INTERNAL)
endif()
set_target_properties(grabCost PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin")
if(WIN32)
  target_compile_definitions(grabCost PRIVATE TRUSSC_SHOW_CONSOLE)
  set_target_properties(grabCost PROPERTIES WIN32_EXECUTABLE FALSE)
endif()
