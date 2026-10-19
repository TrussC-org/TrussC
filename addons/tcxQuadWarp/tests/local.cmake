# Satisfy the addon's drawing symbols without opening a window.
set(_headless_sokol "${TRUSSC_DIR}/tests/common/tcHeadlessSokol.cpp")
target_sources(${PROJECT_NAME} PRIVATE "${_headless_sokol}")
if(APPLE)
    set_source_files_properties("${_headless_sokol}" PROPERTIES LANGUAGE OBJCXX)
endif()
