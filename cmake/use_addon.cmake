# =============================================================================
# use_addon.cmake - TrussC アドオン追加マクロ
# =============================================================================
#
# 使い方:
#   include(${TRUSSC_ROOT}/cmake/use_addon.cmake)
#   add_executable(myapp ...)
#   use_addon(myapp tcxBox2d)
#
# これだけで tcxBox2d アドオンが追加され、myapp にリンクされる。
# =============================================================================

# アドオンが既に追加されているかを追跡
set(_TRUSSC_LOADED_ADDONS "" CACHE INTERNAL "List of loaded TrussC addons")

# use_addon(target addon_name [addon_name2 ...])
# ターゲットに TrussC アドオンを追加してリンクする
macro(use_addon TARGET_NAME)
    foreach(_ADDON_NAME ${ARGN})
        # まだ追加されていないアドオンのみ add_subdirectory
        list(FIND _TRUSSC_LOADED_ADDONS ${_ADDON_NAME} _ADDON_INDEX)
        if(_ADDON_INDEX EQUAL -1)
            # アドオンのパスを解決（TRUSSC_ROOT を使用）
            set(_ADDON_PATH "${TRUSSC_ROOT}/addons/${_ADDON_NAME}")
            if(EXISTS "${_ADDON_PATH}/CMakeLists.txt")
                add_subdirectory("${_ADDON_PATH}" "${CMAKE_BINARY_DIR}/addons/${_ADDON_NAME}")
                list(APPEND _TRUSSC_LOADED_ADDONS ${_ADDON_NAME})
                set(_TRUSSC_LOADED_ADDONS "${_TRUSSC_LOADED_ADDONS}" CACHE INTERNAL "List of loaded TrussC addons")
            else()
                message(FATAL_ERROR "TrussC addon not found: ${_ADDON_NAME}\n  Looked in: ${_ADDON_PATH}")
            endif()
        endif()

        # ターゲットにリンク
        target_link_libraries(${TARGET_NAME} PRIVATE ${_ADDON_NAME})
    endforeach()
endmacro()
