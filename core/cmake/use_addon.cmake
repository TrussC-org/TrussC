# =============================================================================
# use_addon.cmake - TrussC アドオン追加マクロ
# =============================================================================
#
# 使い方1: addons.make を使用（推奨）
#   add_executable(myapp ...)
#   apply_addons(myapp)
#
#   addons.make ファイルにアドオン名を1行ずつ記述:
#     tcxOsc
#     tcxBox2d
#
# 使い方2: 直接指定
#   add_executable(myapp ...)
#   use_addon(myapp tcxBox2d)
#
# これだけで tcxBox2d アドオンが追加され、myapp にリンクされる。
#
# アドオン構造:
#   1. CMakeLists.txt がある場合 → それを使用（フル制御）
#   2. CMakeLists.txt がない場合 → src/ と libs/ を自動収集
#
#   tcxSomeAddon/
#   ├── CMakeLists.txt  (オプション - 特殊処理が必要な場合のみ)
#   ├── src/            ← アドオンのコード (.h + .cpp)
#   └── libs/           ← 外部ライブラリ (git submodule等)
#       └── somelib/
#           ├── src/
#           └── include/
# =============================================================================

# アドオンが既に追加されているかを追跡
set(_TC_LOADED_ADDONS "" CACHE INTERNAL "List of loaded TrussC addons")

# _tc_load_addon(addon_name)
# Create the addon's CMake target without linking it to anything. Used by
# both `use_addon` (which then links to the requested target) and the hot
# reload path (which links the addon's static lib directly to the guest).
macro(_tc_load_addon _ADDON_NAME)
    list(FIND _TC_LOADED_ADDONS ${_ADDON_NAME} _ADDON_INDEX)
    if(_ADDON_INDEX EQUAL -1)
        get_filename_component(_ADDON_PATH "${TRUSSC_DIR}/../addons/${_ADDON_NAME}" ABSOLUTE)

        if(NOT EXISTS "${_ADDON_PATH}")
            message(FATAL_ERROR "TrussC addon not found: ${_ADDON_NAME}\n  Looked in: ${_ADDON_PATH}")
        endif()

        if(EXISTS "${_ADDON_PATH}/CMakeLists.txt")
            add_subdirectory("${_ADDON_PATH}" "${CMAKE_BINARY_DIR}/addons/${_ADDON_NAME}")
        else()
            _tc_auto_addon(${_ADDON_NAME} "${_ADDON_PATH}")
        endif()

        list(APPEND _TC_LOADED_ADDONS ${_ADDON_NAME})
        set(_TC_LOADED_ADDONS "${_TC_LOADED_ADDONS}" CACHE INTERNAL "List of loaded TrussC addons")
    endif()
endmacro()

# use_addon(target addon_name [addon_name2 ...])
# ターゲットに TrussC アドオンを追加してリンクする
macro(use_addon TARGET_NAME)
    foreach(_ADDON_NAME ${ARGN})
        _tc_load_addon(${_ADDON_NAME})
        target_link_libraries(${TARGET_NAME} PRIVATE ${_ADDON_NAME})
    endforeach()
endmacro()

# =============================================================================
# 内部マクロ: 自動収集でアドオンを追加
# =============================================================================
macro(_tc_auto_addon ADDON_NAME ADDON_PATH)
    # ソースファイルを収集
    set(_ADDON_SOURCES "")

    # src/ からソース収集
    if(EXISTS "${ADDON_PATH}/src")
        file(GLOB_RECURSE _SRC_FILES
            "${ADDON_PATH}/src/*.cpp"
            "${ADDON_PATH}/src/*.c"
            "${ADDON_PATH}/src/*.mm"
            "${ADDON_PATH}/src/*.m"
        )
        list(APPEND _ADDON_SOURCES ${_SRC_FILES})
    endif()

    # libs/ からソース収集
    if(EXISTS "${ADDON_PATH}/libs")
        file(GLOB _LIB_DIRS LIST_DIRECTORIES true "${ADDON_PATH}/libs/*")
        foreach(_LIB_DIR ${_LIB_DIRS})
            if(IS_DIRECTORY ${_LIB_DIR})
                if(EXISTS "${_LIB_DIR}/src")
                    file(GLOB_RECURSE _LIB_SRC_FILES
                        "${_LIB_DIR}/src/*.cpp"
                        "${_LIB_DIR}/src/*.c"
                        "${_LIB_DIR}/src/*.mm"
                        "${_LIB_DIR}/src/*.m"
                    )
                    list(APPEND _ADDON_SOURCES ${_LIB_SRC_FILES})
                endif()
            endif()
        endforeach()
    endif()

    # ソースがない場合はヘッダオンリーライブラリとして作成
    if(_ADDON_SOURCES)
        add_library(${ADDON_NAME} STATIC ${_ADDON_SOURCES})
    else()
        add_library(${ADDON_NAME} INTERFACE)
    endif()

    # インクルードパスを設定
    set(_ADDON_INCLUDE_DIRS "")

    # include/ フォルダ
    if(EXISTS "${ADDON_PATH}/include")
        list(APPEND _ADDON_INCLUDE_DIRS "${ADDON_PATH}/include")
    endif()

    # src/ フォルダ（ヘッダがある場合）
    if(EXISTS "${ADDON_PATH}/src")
        list(APPEND _ADDON_INCLUDE_DIRS "${ADDON_PATH}/src")
    endif()

    # libs/*/include フォルダ
    if(EXISTS "${ADDON_PATH}/libs")
        file(GLOB _LIB_DIRS LIST_DIRECTORIES true "${ADDON_PATH}/libs/*")
        foreach(_LIB_DIR ${_LIB_DIRS})
            if(IS_DIRECTORY ${_LIB_DIR})
                if(EXISTS "${_LIB_DIR}/include")
                    list(APPEND _ADDON_INCLUDE_DIRS "${_LIB_DIR}/include")
                endif()
                if(EXISTS "${_LIB_DIR}/src")
                    list(APPEND _ADDON_INCLUDE_DIRS "${_LIB_DIR}/src")
                endif()
            endif()
        endforeach()
    endif()

    # インクルードパスを適用
    if(_ADDON_SOURCES)
        target_include_directories(${ADDON_NAME} PUBLIC ${_ADDON_INCLUDE_DIRS})
        target_link_libraries(${ADDON_NAME} PUBLIC TrussC)
    else()
        target_include_directories(${ADDON_NAME} INTERFACE ${_ADDON_INCLUDE_DIRS})
    endif()

    message(STATUS "[${ADDON_NAME}] Auto-configured addon")
endmacro()

# =============================================================================
# apply_addons - addons.make からアドオンを自動適用
# =============================================================================
# addons.make ファイルを読み込み、記述されたアドオンを全て適用する
# コメント（#で始まる行）と空行は無視される
macro(apply_addons TARGET_NAME)
    set(_ADDONS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/addons.make")
    if(EXISTS "${_ADDONS_FILE}")
        file(STRINGS "${_ADDONS_FILE}" _ADDON_LINES)
        foreach(_LINE ${_ADDON_LINES})
            # 空白をトリム
            string(STRIP "${_LINE}" _LINE)
            # 空行とコメント行をスキップ
            if(_LINE AND NOT _LINE MATCHES "^#")
                use_addon(${TARGET_NAME} ${_LINE})
            endif()
        endforeach()
    endif()
endmacro()

# =============================================================================
# tc_addon_bundle_file - addon が必要とするランタイムファイルをアプリに同梱する
# =============================================================================
# アドオンの CMakeLists.txt から呼び出すと、指定ファイルを最終的なアプリの出力に
# 同梱する。プラットフォームごとに適切な場所へコピーされる:
#   - macOS : App.app/Contents/<MACOS_DEST>  (既定: Resources)
#   - Windows / Linux : 実行ファイルと同じディレクトリ (DLL/so の隣置き用)
#
# 実体のコピーは trussc_app() 側（アプリターゲットと同じスコープ）で行うため、
# ここでは GLOBAL プロパティに登録するだけにしておき、cross-directory の
# ターゲット変更を避ける。
#
# 使い方（アドオンの CMakeLists.txt 内）:
#   tc_addon_bundle_file(${CMAKE_CURRENT_BINARY_DIR}/default.metallib MACOS_DEST Resources)
#   tc_addon_bundle_file(${CMAKE_CURRENT_SOURCE_DIR}/libs/sensor/sensor.dll)
function(tc_addon_bundle_file _FILE)
    cmake_parse_arguments(_TCB "" "MACOS_DEST" "" ${ARGN})
    if(NOT _TCB_MACOS_DEST)
        set(_TCB_MACOS_DEST "Resources")
    endif()
    get_filename_component(_TCB_ABS "${_FILE}" ABSOLUTE)
    # "<macos-dest>|<absolute-path>" の形で蓄積する（パスに | は来ない想定）
    set_property(GLOBAL APPEND PROPERTY TC_ADDON_BUNDLE_FILES "${_TCB_MACOS_DEST}|${_TCB_ABS}")
endfunction()
