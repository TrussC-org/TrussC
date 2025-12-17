# =============================================================================
# box2dNodeExample - tcxBox2d + Node 統合サンプル
# =============================================================================

set(EXAMPLE_NAME box2dNodeExample)

set(EXAMPLE_SOURCES
    src/tcApp.cpp
)

if(APPLE)
    add_executable(${EXAMPLE_NAME} MACOSX_BUNDLE ${EXAMPLE_SOURCES})

    set_target_properties(${EXAMPLE_NAME} PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME ${EXAMPLE_NAME}
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.trussc.${EXAMPLE_NAME}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
        MACOSX_BUNDLE_BUNDLE_VERSION "1"
        # bin/ フォルダに出力（oF スタイル）
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    )
else()
    add_executable(${EXAMPLE_NAME} ${EXAMPLE_SOURCES})
    set_target_properties(${EXAMPLE_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
    )
endif()

target_link_libraries(${EXAMPLE_NAME} PRIVATE
    tcxBox2d
)

# アイコン設定
trussc_setup_icon(${EXAMPLE_NAME})
