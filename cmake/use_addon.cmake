# =============================================================================
# use_addon.cmake - TrussC アドオン追加マクロ
# =============================================================================
#
# 使い方:
#   include(${TC_ROOT}/cmake/use_addon.cmake)
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
#   ├── CMakeLists.txt  (オプション)
#   ├── src/            ← ソース自動収集 (*.cpp, *.c, *.mm)
#   ├── include/        ← インクルードパス自動追加
#   └── libs/           ← サードパーティ自動収集
#       └── somelib/
#           ├── src/
#           └── include/
# =============================================================================

# アドオンが既に追加されているかを追跡
set(_TC_LOADED_ADDONS "" CACHE INTERNAL "List of loaded TrussC addons")

# use_addon(target addon_name [addon_name2 ...])
# ターゲットに TrussC アドオンを追加してリンクする
macro(use_addon TARGET_NAME)
    foreach(_ADDON_NAME ${ARGN})
        # まだ追加されていないアドオンのみ処理
        list(FIND _TC_LOADED_ADDONS ${_ADDON_NAME} _ADDON_INDEX)
        if(_ADDON_INDEX EQUAL -1)
            # アドオンのパスを解決（TC_ROOT を使用）
            set(_ADDON_PATH "${TC_ROOT}/addons/${_ADDON_NAME}")

            if(NOT EXISTS "${_ADDON_PATH}")
                message(FATAL_ERROR "TrussC addon not found: ${_ADDON_NAME}\n  Looked in: ${_ADDON_PATH}")
            endif()

            if(EXISTS "${_ADDON_PATH}/CMakeLists.txt")
                # CMakeLists.txt がある場合 → それを使用
                add_subdirectory("${_ADDON_PATH}" "${CMAKE_BINARY_DIR}/addons/${_ADDON_NAME}")
            else()
                # CMakeLists.txt がない場合 → 自動収集
                _tc_auto_addon(${_ADDON_NAME} "${_ADDON_PATH}")
            endif()

            list(APPEND _TC_LOADED_ADDONS ${_ADDON_NAME})
            set(_TC_LOADED_ADDONS "${_TC_LOADED_ADDONS}" CACHE INTERNAL "List of loaded TrussC addons")
        endif()

        # ターゲットにリンク
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
