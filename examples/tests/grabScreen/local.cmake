# Build the shipped example source into this self-check (kept here because trusscli update regenerates CMakeLists.txt).
target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../graphics/grabScreenExample/src/tcApp.cpp)
