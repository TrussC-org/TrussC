# Reuse the configured tools dependencies; trussc_app's check targets are
# directory-global, so a second invocation would collide with the CLI's.
file(GLOB shared_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../src/*.cpp")
list(FILTER shared_sources EXCLUDE REGEX "/(main|tcApp)\\.cpp$")
add_executable(trusscli-gui2 src/main.cpp src/ProjectHub.cpp ${shared_sources})
foreach(prop LINK_LIBRARIES LINK_OPTIONS INCLUDE_DIRECTORIES COMPILE_DEFINITIONS COMPILE_OPTIONS)
    get_target_property(value trusscli ${prop})
    if(value)
        set_property(TARGET trusscli-gui2 PROPERTY ${prop} "${value}")
    endif()
endforeach()
target_include_directories(trusscli-gui2 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
target_compile_features(trusscli-gui2 PRIVATE cxx_std_20)
add_dependencies(trusscli-gui2 _tc_hot_reload_check)
foreach(prop RUNTIME_OUTPUT_DIRECTORY RUNTIME_OUTPUT_DIRECTORY_RELEASE
             RUNTIME_OUTPUT_DIRECTORY_DEBUG RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO
             RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL)
    if(DEFINED CMAKE_${prop})
        set_target_properties(trusscli-gui2 PROPERTIES ${prop} "${CMAKE_${prop}}")
    else()
        set_target_properties(trusscli-gui2 PROPERTIES ${prop} "${CMAKE_CURRENT_SOURCE_DIR}/../bin")
    endif()
endforeach()
if(APPLE)
    set_target_properties(trusscli-gui2 PROPERTIES MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.trussc.trusscli-gui2"
        MACOSX_BUNDLE_BUNDLE_NAME "TrussC")
elseif(WIN32)
    target_link_libraries(trusscli-gui2 PRIVATE shell32 ole32 uuid)
    trussc_setup_manifest(trusscli-gui2)
endif()
