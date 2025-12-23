cmake_minimum_required(VERSION 3.20)

set(TRUSSC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../trussc")
include(${TRUSSC_DIR}/cmake/trussc_app.cmake)

trussc_app()

# Camera permission description (required for macOS 10.14+)
if(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        XCODE_ATTRIBUTE_INFOPLIST_KEY_NSCameraUsageDescription "This app uses the camera for video capture."
    )
endif()
