cmake_minimum_required(VERSION 3.20)

set(TRUSSC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..\..\..\trussc")
include(${TRUSSC_DIR}/cmake/trussc_app.cmake)

trussc_app()
