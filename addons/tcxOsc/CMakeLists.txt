# =============================================================================
# tcxOsc - TrussC OSC アドオン
# =============================================================================

cmake_minimum_required(VERSION 3.16)

# アドオン名
set(ADDON_NAME tcxOsc)

# =============================================================================
# アドオンライブラリ
# =============================================================================
set(ADDON_SOURCES
    src/tcxOscMessage.cpp
    src/tcxOscBundle.cpp
)

set(ADDON_HEADERS
    include/tcxOsc/tcxOsc.h
    include/tcxOsc/tcxOscMessage.h
    include/tcxOsc/tcxOscBundle.h
    include/tcxOsc/tcxOscSender.h
    include/tcxOsc/tcxOscReceiver.h
)

add_library(${ADDON_NAME} STATIC ${ADDON_SOURCES} ${ADDON_HEADERS})

target_include_directories(${ADDON_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(${ADDON_NAME} PUBLIC
    TrussC
)

# =============================================================================
# サンプルプロジェクト
# =============================================================================
option(TCX_OSC_BUILD_EXAMPLES "Build tcxOsc examples" ON)

if(TCX_OSC_BUILD_EXAMPLES)
    add_subdirectory(examples/oscExample)
endif()
