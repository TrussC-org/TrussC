# Headless texture tests use dummy GPU resources, with no window or device.
set(_headless_sokol "${TRUSSC_DIR}/tests/common/tcHeadlessSokol.cpp")
target_sources(${PROJECT_NAME} PRIVATE "${_headless_sokol}")
if(APPLE)
    set_source_files_properties("${_headless_sokol}" PROPERTIES LANGUAGE OBJCXX)
endif()

# Exercise upstream result codes with a test-only memory callback.
FetchContent_GetProperties(cgltf SOURCE_DIR _gltf_test_source)
target_include_directories(${PROJECT_NAME} PRIVATE "${_gltf_test_source}")
