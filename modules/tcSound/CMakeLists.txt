# =============================================================================
# tcSound モジュール
# OGG/WAV/MP3 サウンド再生 + マイク入力機能
# =============================================================================

add_library(tcSound STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tcSound_impl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/tcMicInput_impl.cpp
)

add_library(tc::Sound ALIAS tcSound)

# インクルードパス
target_include_directories(tcSound PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# TrussC 本体への依存（sokol_audio を使用）
target_link_libraries(tcSound PUBLIC TrussC)

# プラットフォーム固有の設定
if(APPLE)
    target_link_libraries(tcSound PUBLIC
        "-framework AudioToolbox"
        "-framework CoreAudio"
        "-framework CoreFoundation"
    )
endif()
