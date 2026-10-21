if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    # Compile the real platform implementation through a test wrapper. No test
    # seam or interception is present in normal TrussC binaries.
    get_target_property(_metal_growth_sources TrussC SOURCES)
    list(FILTER _metal_growth_sources EXCLUDE REGEX "platform/mac/sokol_impl\\.mm$")
    set_property(TARGET TrussC PROPERTY SOURCES "${_metal_growth_sources}")
    target_sources(TrussC PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/metalProbe.mm")
elseif(APPLE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE TC_METAL_GROWTH_SKIP)
endif()
