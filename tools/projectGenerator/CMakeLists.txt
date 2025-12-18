# =============================================================================
# projectGenerator - TrussC プロジェクト生成ツール
# =============================================================================

cmake_minimum_required(VERSION 3.20)
project(projectGenerator LANGUAGES C CXX OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TRUSSC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")
message(STATUS "[projectGenerator] TrussC: ${TRUSSC_DIR}")

add_subdirectory(${TRUSSC_DIR} ${CMAKE_BINARY_DIR}/TrussC)

add_executable(${PROJECT_NAME})

# ソースファイル（src/ 内を自動収集）
file(GLOB_RECURSE PROJECT_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.mm"
)
target_sources(${PROJECT_NAME} PRIVATE ${PROJECT_SOURCES})

target_link_libraries(${PROJECT_NAME} PRIVATE tc::TrussC)

if(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "TrussC Project Generator"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.trussc.projectGenerator"
        MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    )
    trussc_setup_icon(${PROJECT_NAME})
else()
    set_target_properties(${PROJECT_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    )
endif()
