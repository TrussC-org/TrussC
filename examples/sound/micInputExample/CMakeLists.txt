cmake_minimum_required(VERSION 3.20)

set(TRUSSC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../trussc")
include(${TRUSSC_DIR}/cmake/trussc_app.cmake)

trussc_app()

# Microphone permission for macOS
if(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
    )
endif()
