# Reuse the parser translation unit also compiled by externalMainDelivery.
# CMake deduplicates the same source path in the combined core test target.
target_sources(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../externalMainDelivery/src/osc.cpp")
