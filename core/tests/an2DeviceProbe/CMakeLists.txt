# Handwritten entry point for the disposable #239 diagnostic probe.
cmake_minimum_required(VERSION 3.20)
project(an2DeviceProbe LANGUAGES C CXX)
set(TRUSSC_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH "TrussC core directory")
include("${TRUSSC_DIR}/cmake/trussc_app.cmake")
trussc_app()
