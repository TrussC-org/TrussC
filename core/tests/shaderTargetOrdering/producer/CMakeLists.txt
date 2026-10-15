# Copy into this build tree so every clean build starts without generated headers.
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/../../hotReloadLifecycle/src/shaders/reloadProbe.glsl"
    "${CMAKE_CURRENT_BINARY_DIR}/source/probe.glsl" COPYONLY)
trussc_compile_shaders(producer "${CMAKE_CURRENT_BINARY_DIR}/source"
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
