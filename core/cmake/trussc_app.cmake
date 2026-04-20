# =============================================================================
# trussc_app.cmake - TrussC application setup macro
# =============================================================================
#
# Usage:
#   set(TRUSSC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../core")
#   include(${TRUSSC_DIR}/cmake/trussc_app.cmake)
#   trussc_app()
#
# This is all you need to build a TrussC app.
# To use addons, list addon names in addons.make file.
# For project-specific CMake config, create local.cmake in project root.
#
# Options:
#   trussc_app(SOURCES file1.cpp file2.cpp)  # Explicit source files
#   trussc_app(NAME myapp)                    # Explicit project name
#
# Shader compilation:
#   If src/**/*.glsl files exist, they are automatically compiled
#   with sokol-shdc to generate cross-platform shader headers.
#
# =============================================================================

macro(trussc_app)
    # Set default build type to RelWithDebInfo
    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
    endif()

    # Parse arguments
    set(_options "")
    set(_oneValueArgs NAME DISPLAY_NAME)
    set(_multiValueArgs SOURCES)
    cmake_parse_arguments(_TC_APP "${_options}" "${_oneValueArgs}" "${_multiValueArgs}" ${ARGN})

    # Project name (use folder name if not specified)
    if(_TC_APP_NAME)
        set(_TC_PROJECT_NAME ${_TC_APP_NAME})
    else()
        get_filename_component(_TC_PROJECT_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
    endif()

    # Display name for GUI (Finder/Dock on macOS, app menu on iOS).
    # Defaults to the project name; override with DISPLAY_NAME when the binary
    # name should differ from the human-readable name (e.g. a CLI tool whose
    # GUI mode shows a friendlier label).
    if(_TC_APP_DISPLAY_NAME)
        set(MACOSX_BUNDLE_DISPLAY_NAME "${_TC_APP_DISPLAY_NAME}")
    else()
        set(MACOSX_BUNDLE_DISPLAY_NAME "${_TC_PROJECT_NAME}")
    endif()

    # Check for target collision (for batch builds)
    if(TARGET ${_TC_PROJECT_NAME})
        get_filename_component(_TC_PARENT_DIR ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)
        get_filename_component(_TC_PARENT_NAME ${_TC_PARENT_DIR} NAME)
        set(_TC_PROJECT_NAME "${_TC_PARENT_NAME}_${_TC_PROJECT_NAME}")
        message(STATUS "Target name collision detected. Renamed to: ${_TC_PROJECT_NAME}")
    endif()

    # Set languages based on platform
    if(APPLE)
        project(${_TC_PROJECT_NAME} LANGUAGES C CXX OBJC OBJCXX)
    else()
        project(${_TC_PROJECT_NAME} LANGUAGES C CXX)
    endif()

    # C++20
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    # Add TrussC (shared build directory for faster subsequent builds)
    # Separate directories per platform to avoid conflicts (e.g. Dropbox sync)
    if(EMSCRIPTEN)
        set(_TC_BUILD_DIR "${TRUSSC_DIR}/build-web")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        set(_TC_BUILD_DIR "${TRUSSC_DIR}/build-ios")
    elseif(ANDROID)
        set(_TC_BUILD_DIR "${TRUSSC_DIR}/build-android")
    elseif(APPLE)
        set(_TC_BUILD_DIR "${TRUSSC_DIR}/build-macos")
    elseif(WIN32)
        set(_TC_BUILD_DIR "${TRUSSC_DIR}/build-windows")
    else()
        set(_TC_BUILD_DIR "${TRUSSC_DIR}/build-linux")
    endif()
    
    if(NOT TARGET TrussC)
        add_subdirectory(${TRUSSC_DIR} ${_TC_BUILD_DIR})
    endif()

    # Source files (auto-collect from src/ recursively if not specified)
    if(_TC_APP_SOURCES)
        set(_TC_SOURCES ${_TC_APP_SOURCES})
    else()
        file(GLOB_RECURSE _TC_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/*.mm"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/*.m"
        )
        # Exclude Objective-C/C++ files on non-Apple platforms
        if(NOT APPLE)
            list(FILTER _TC_SOURCES EXCLUDE REGEX ".*\\.mm$")
            list(FILTER _TC_SOURCES EXCLUDE REGEX ".*\\.m$")
        endif()
    endif()

    # Preserve directory structure in Xcode / Visual Studio
    source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "src" FILES ${_TC_SOURCES})

    # =========================================================================
    # Hot Reload detection
    # Scan source files for TC_HOT_RELOAD macro. If found on a supported
    # platform, split the build into Host (EXE) + Guest (shared library).
    #
    # A state file (.tc_hot_reload_state) in the build dir tracks the
    # previous detection result. A pre-build custom command re-scans the
    # sources; if the result changed (or the state file doesn't exist),
    # it touches CMakeLists.txt to force cmake to reconfigure automatically.
    # This means: adding or removing TC_HOT_RELOAD → just rebuild, no
    # manual reconfig needed.
    # =========================================================================
    set(_TC_HOT_RELOAD OFF)
    if(NOT EMSCRIPTEN AND NOT ANDROID AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
        foreach(_src ${_TC_SOURCES})
            if(_src MATCHES "\\.cpp$")
                file(READ "${_src}" _content)
                # Match TC_HOT_RELOAD that is NOT on a comment line
                string(REGEX MATCH "\n[^/\n]*TC_HOT_RELOAD" _match "${_content}")
                if(_match)
                    set(_TC_HOT_RELOAD ON)
                    break()
                endif()
                # Also check first line (no preceding newline)
                string(REGEX MATCH "^[^/\n]*TC_HOT_RELOAD" _match2 "${_content}")
                if(_match2)
                    set(_TC_HOT_RELOAD ON)
                    break()
                endif()
            endif()
        endforeach()
    endif()

    # Save the detection result to the state file
    set(_TC_HR_STATE_FILE "${CMAKE_BINARY_DIR}/.tc_hot_reload_state")
    file(WRITE "${_TC_HR_STATE_FILE}" "${_TC_HOT_RELOAD}")

    # Generate a CMake script that re-scans sources at build time and
    # triggers reconfig if the result changed. Using a CMake script (rather
    # than a shell script) keeps this cross-platform — Windows doesn't ship
    # `sh` by default.
    set(_TC_HR_CHECK_SCRIPT "${CMAKE_BINARY_DIR}/_tc_check_hot_reload.cmake")
    set(_TC_HR_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
    set(_TC_HR_CMAKELISTS "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt")
    file(WRITE "${_TC_HR_CHECK_SCRIPT}"
"# Auto-generated by trussc_app.cmake -- checks if TC_HOT_RELOAD state changed.
set(STATE_FILE \"${_TC_HR_STATE_FILE}\")
set(SRC_DIR    \"${_TC_HR_SRC_DIR}\")
set(CMAKELISTS \"${_TC_HR_CMAKELISTS}\")

set(PREV \"\")
if(EXISTS \"\${STATE_FILE}\")
    file(READ \"\${STATE_FILE}\" PREV)
    string(STRIP \"\${PREV}\" PREV)
endif()

# Scan for uncommented TC_HOT_RELOAD in any .cpp under SRC_DIR
set(CURRENT \"OFF\")
file(GLOB _CPPS \"\${SRC_DIR}/*.cpp\")
foreach(_f \${_CPPS})
    file(STRINGS \"\${_f}\" _LINES REGEX \"TC_HOT_RELOAD\")
    foreach(_line \${_LINES})
        string(REGEX MATCH \"^[ \\t]*//\" _is_comment \"\${_line}\")
        if(NOT _is_comment)
            set(CURRENT \"ON\")
            break()
        endif()
    endforeach()
    if(CURRENT STREQUAL \"ON\")
        break()
    endif()
endforeach()

if(NOT \"\${PREV}\" STREQUAL \"\${CURRENT}\")
    if(CURRENT STREQUAL \"ON\")
        message(\"\")
        message(\"  [HotReload] TC_HOT_RELOAD detected.\")
    else()
        message(\"\")
        message(\"  [HotReload] TC_HOT_RELOAD removed.\")
    endif()
    message(\"  Use 'trusscli build' for automatic reconfig, or build again with cmake.\")
    message(\"\")
    file(TOUCH \"\${CMAKELISTS}\")
    message(FATAL_ERROR \"hot reload state changed -- reconfigure required\")
endif()
")

    # Add a pre-build check that runs every build (cost: ~5ms scan)
    add_custom_target(_tc_hot_reload_check ALL
        COMMAND ${CMAKE_COMMAND} -P "${_TC_HR_CHECK_SCRIPT}"
        COMMENT ""
        VERBATIM
    )

    # Clean up old guest libraries when hot reload is disabled
    if(NOT _TC_HOT_RELOAD)
        file(GLOB _TC_OLD_GUESTS "${CMAKE_BINARY_DIR}/libguest.*")
        if(_TC_OLD_GUESTS)
            file(REMOVE ${_TC_OLD_GUESTS})
            message(STATUS "[${_TC_PROJECT_NAME}] Cleaned up old hot reload guest library")
        endif()
    endif()

    # Create target
    if(_TC_HOT_RELOAD)
        message(STATUS "[${_TC_PROJECT_NAME}] Hot reload enabled — building Host + Guest")

        # Split sources: main.cpp → Host, everything else → Guest
        set(_TC_HOST_SOURCES "")
        set(_TC_GUEST_SOURCES "")
        foreach(_src ${_TC_SOURCES})
            get_filename_component(_fname "${_src}" NAME)
            if(_fname STREQUAL "main.cpp")
                list(APPEND _TC_HOST_SOURCES "${_src}")
            else()
                list(APPEND _TC_GUEST_SOURCES "${_src}")
            endif()
        endforeach()

        if(NOT _TC_HOST_SOURCES)
            message(FATAL_ERROR "[${_TC_PROJECT_NAME}] Hot reload requires main.cpp in src/")
        endif()

        # Guest: shared library with user code
        add_library(guest SHARED ${_TC_GUEST_SOURCES})
        target_include_directories(guest PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
            "${TRUSSC_DIR}/include"
        )
        target_compile_features(guest PRIVATE cxx_std_20)
        target_compile_definitions(guest PRIVATE TC_HOT_RELOAD_BUILD)
        # GuestはTrussCにリンクしない（シンボルはHostから実行時解決）が、
        # TrussCのPUBLICコンパイル設定（/utf-8, Windows SDK include, SOKOL_D3D11等）は必要。
        get_target_property(_TC_PUB_OPTS TrussC INTERFACE_COMPILE_OPTIONS)
        if(_TC_PUB_OPTS)
            target_compile_options(guest PRIVATE ${_TC_PUB_OPTS})
        endif()
        get_target_property(_TC_PUB_DEFS TrussC INTERFACE_COMPILE_DEFINITIONS)
        if(_TC_PUB_DEFS)
            target_compile_definitions(guest PRIVATE ${_TC_PUB_DEFS})
        endif()
        get_target_property(_TC_PUB_INCS TrussC INTERFACE_INCLUDE_DIRECTORIES)
        if(_TC_PUB_INCS)
            target_include_directories(guest PRIVATE ${_TC_PUB_INCS})
        endif()
        get_target_property(_TC_SYS_INCS TrussC INTERFACE_SYSTEM_INCLUDE_DIRECTORIES)
        if(_TC_SYS_INCS)
            target_include_directories(guest SYSTEM PRIVATE ${_TC_SYS_INCS})
        endif()
        get_target_property(_TC_PUB_LINK_DIRS TrussC INTERFACE_LINK_DIRECTORIES)
        if(_TC_PUB_LINK_DIRS)
            target_link_directories(guest PRIVATE ${_TC_PUB_LINK_DIRS})
        endif()
        # Windows: デバッグ情報を.objに埋め込み（/Z7）、PDBを生成しない。
        # デバッガがguest.pdbをロックするとホットリロード時のリビルドが失敗するため。
        if(MSVC)
            target_compile_options(guest PRIVATE /Z7)
            set_target_properties(guest PROPERTIES LINK_FLAGS "/DEBUG /PDBALTPATH:none")
        endif()
        # Guest: disable codegen optimization to speed up reload iterations.
        # Host and TrussC keep the project's configured optimization level;
        # only the hot-reload Guest trades runtime speed for compile speed.
        # Trailing position ensures these flags override the earlier -O2/-O3
        # inherited from CMAKE_CXX_FLAGS_<CONFIG>.
        if(MSVC)
            target_compile_options(guest PRIVATE /Od)
        else()
            target_compile_options(guest PRIVATE -O0)
        endif()
        # Guest: resolve TrussC symbols at runtime from the Host process.
        # macOS/Linux use flat namespace lookup; Windows uses import library.
        if(APPLE)
            target_link_options(guest PRIVATE -undefined dynamic_lookup)
        elseif(WIN32)
            # Windows: Guest links Host's import library (set below)
        else()
            # Linux: analogue of macOS `-undefined dynamic_lookup`. The guest
            # deliberately leaves TrussC/sokol symbols unresolved at link time;
            # dlopen(..., RTLD_NOW) resolves them against the Host's dynamic
            # symbol table (Host is linked with -rdynamic and --whole-archive
            # TrussC). `ignore-in-shared-libs` is NOT enough here because the
            # symbols live in a static archive on the Host, not in a .so.
            target_link_options(guest PRIVATE -Wl,--unresolved-symbols=ignore-all)
        endif()
        # Guest output directory
        set_target_properties(guest PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        )

        # Host: executable with main.cpp + TrussC core
        add_executable(${PROJECT_NAME} ${_TC_HOST_SOURCES})
        target_compile_definitions(${PROJECT_NAME} PRIVATE TC_HOT_RELOAD_BUILD)
        set_target_properties(${PROJECT_NAME} PROPERTIES ENABLE_EXPORTS TRUE)
        # Force-load ALL symbols from libTrussC.a into the Host (not just the ones
        # main.cpp uses) AND export them in the dynamic symbol table. This makes
        # every TrussC function visible to the Guest at runtime.
        if(APPLE)
            target_link_options(${PROJECT_NAME} PRIVATE
                -Wl,-force_load,$<TARGET_FILE:TrussC>
                -Wl,-export_dynamic)
        elseif(WIN32)
            # Windows Hot Reload:
            # TrussC.libの全シンボルをHostからエクスポートし、GuestのDLLがリンクできるようにする。
            # WINDOWS_EXPORT_ALL_SYMBOLS は static lib のシンボルをスキャンしないため、
            # dumpbin で TrussC.lib からシンボルを抽出し .def ファイルを生成する。
            set(_TC_DEF_SCRIPT "${CMAKE_BINARY_DIR}/_tc_gen_exports.cmake")
            set(_TC_DEF_FILE "${CMAKE_BINARY_DIR}/trussc_exports.def")
            file(WRITE "${_TC_DEF_SCRIPT}"
"# TrussC.lib から全エクスポートシンボルを抽出して .def を生成する
set(LIB_FILE \"\${LIB_FILE}\")
set(DEF_FILE \"\${DEF_FILE}\")
set(DUMPBIN \"\${DUMPBIN}\")

execute_process(
    COMMAND \${DUMPBIN} /LINKERMEMBER:1 \${LIB_FILE}
    OUTPUT_VARIABLE DUMP_OUT
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# public symbols セクションからシンボル名を抽出
# 実際のシンボル行は「  XXXXXX シンボル名」の形式（6桁以上のhexアドレス）
set(SYMBOLS \"\")
string(REGEX MATCHALL \"[0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f]+ [?_@A-Za-z][^ \\n]*\" ENTRIES \"\${DUMP_OUT}\")
foreach(ENTRY \${ENTRIES})
    string(REGEX MATCH \"^[0-9A-Fa-f]+ (.+)\" _ \"\${ENTRY}\")
    if(CMAKE_MATCH_1)
        set(SYM \"\${CMAKE_MATCH_1}\")
        # フィルタ: エクスポート不可能なシンボルをスキップ
        if(SYM MATCHES \"^__imp_\")
            # インポートサンク
        elseif(SYM MATCHES \"^[.]\")
            # セクション名 (.CRT$XCU, .rdata 等)
        elseif(SYM MATCHES \"/\")
            # dumpbinヘッダーのメタデータ (time/date 等)
        elseif(SYM MATCHES \"^__NULL_IMPORT\")
            # NULLインポート記述子
        elseif(SYM MATCHES \"^__IMPORT_DESCRIPTOR\")
            # インポート記述子
        elseif(SYM MATCHES \"_NULL_THUNK_DATA$\")
            # NULLサンクデータ
        elseif(SYM MATCHES \"^_RTC_\")
            # ランタイムチェック用内部シンボル
        else()
            list(APPEND SYMBOLS \"\${SYM}\")
        endif()
    endif()
endforeach()

list(REMOVE_DUPLICATES SYMBOLS)

# .def ファイル書き出し
file(WRITE \"\${DEF_FILE}\" \"EXPORTS\\n\")
foreach(SYM \${SYMBOLS})
    file(APPEND \"\${DEF_FILE}\" \"    \${SYM}\\n\")
endforeach()

list(LENGTH SYMBOLS SYM_COUNT)
message(\"  [HotReload] Generated \${DEF_FILE} with \${SYM_COUNT} symbols\")
")
            # dumpbin.exe のパスを検出
            get_filename_component(_TC_MSVC_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
            find_program(_TC_DUMPBIN dumpbin HINTS "${_TC_MSVC_BIN_DIR}")
            if(NOT _TC_DUMPBIN)
                message(FATAL_ERROR
                    "[HotReload] dumpbin.exe not found. Hot Reload on Windows "
                    "requires the MSVC toolchain (looked near "
                    "${_TC_MSVC_BIN_DIR}). Open a Visual Studio Developer "
                    "Prompt or run vcvarsall.bat before configuring.")
            endif()

            # TrussC.lib ビルド後に .def を生成
            add_custom_command(
                OUTPUT "${_TC_DEF_FILE}"
                COMMAND ${CMAKE_COMMAND}
                    -DLIB_FILE=$<TARGET_FILE:TrussC>
                    -DDEF_FILE=${_TC_DEF_FILE}
                    -DDUMPBIN=${_TC_DUMPBIN}
                    -P "${_TC_DEF_SCRIPT}"
                DEPENDS TrussC
                COMMENT "[HotReload] Generating export definitions from TrussC.lib"
                VERBATIM
            )
            add_custom_target(_tc_gen_exports DEPENDS "${_TC_DEF_FILE}")
            add_dependencies(${PROJECT_NAME} _tc_gen_exports)

            # Host に /DEF: と /WHOLEARCHIVE を渡す
            target_link_options(${PROJECT_NAME} PRIVATE
                /DEF:${_TC_DEF_FILE}
                /WHOLEARCHIVE:$<TARGET_FILE:TrussC>
            )
            target_link_libraries(guest PRIVATE ${PROJECT_NAME})
        else()
            target_link_options(${PROJECT_NAME} PRIVATE
                -Wl,--whole-archive $<TARGET_FILE:TrussC> -Wl,--no-whole-archive
                -rdynamic)
        endif()

    elseif(ANDROID)
        add_library(${PROJECT_NAME} SHARED ${_TC_SOURCES})
    else()
        add_executable(${PROJECT_NAME} ${_TC_SOURCES})
    endif()

    # Link TrussC
    target_link_libraries(${PROJECT_NAME} PRIVATE tc::TrussC)

    # Ensure the hot reload check runs BEFORE any compilation starts.
    # If the check fails (state changed), the build stops immediately
    # with a clear message — no wasted compilation time.
    if(TARGET _tc_hot_reload_check)
        add_dependencies(${PROJECT_NAME} _tc_hot_reload_check)
        if(TARGET guest)
            add_dependencies(guest _tc_hot_reload_check)
        endif()
    endif()

    # Apply addons from addons.make
    apply_addons(${PROJECT_NAME})

    # Include project-local CMake config if it exists
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/local.cmake")
        include("${CMAKE_CURRENT_SOURCE_DIR}/local.cmake")
        message(STATUS "[${PROJECT_NAME}] Loaded local.cmake")
    endif()

    # Compile shaders with sokol-shdc (if any .glsl files exist)
    file(GLOB_RECURSE _TC_SHADER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.glsl")
    if(_TC_SHADER_SOURCES)
        # Select sokol-shdc binary based on host platform
        # Download from official sokol-tools-bin repository
        set(_TC_SOKOL_SHDC_BASE_URL "https://raw.githubusercontent.com/floooh/sokol-tools-bin/master/bin")
        if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
            if(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64")
                set(_TC_SOKOL_SHDC_DIR "osx_arm64")
            else()
                set(_TC_SOKOL_SHDC_DIR "osx")
            endif()
        elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
            set(_TC_SOKOL_SHDC_DIR "win32")
        else()
            if(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "aarch64")
                set(_TC_SOKOL_SHDC_DIR "linux_arm64")
            else()
                set(_TC_SOKOL_SHDC_DIR "linux")
            endif()
        endif()
        # Windows uses .exe extension
        if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
            set(_TC_SOKOL_SHDC_EXT ".exe")
        else()
            set(_TC_SOKOL_SHDC_EXT "")
        endif()
        set(_TC_SOKOL_SHDC_URL "${_TC_SOKOL_SHDC_BASE_URL}/${_TC_SOKOL_SHDC_DIR}/sokol-shdc${_TC_SOKOL_SHDC_EXT}")
        set(_TC_SOKOL_SHDC_NAME "sokol-shdc${_TC_SOKOL_SHDC_EXT}")

        set(_TC_SOKOL_SHDC "${TC_ROOT}/core/tools/sokol-shdc/${_TC_SOKOL_SHDC_NAME}")

        # Download sokol-shdc if not present
        if(NOT EXISTS "${_TC_SOKOL_SHDC}")
            message(STATUS "[${PROJECT_NAME}] Downloading sokol-shdc...")
            file(MAKE_DIRECTORY "${TC_ROOT}/core/tools/sokol-shdc")
            file(DOWNLOAD "${_TC_SOKOL_SHDC_URL}" "${_TC_SOKOL_SHDC}"
                SHOW_PROGRESS
                STATUS _TC_DOWNLOAD_STATUS)
            list(GET _TC_DOWNLOAD_STATUS 0 _TC_DOWNLOAD_ERROR)
            if(_TC_DOWNLOAD_ERROR)
                message(FATAL_ERROR "Failed to download sokol-shdc: ${_TC_DOWNLOAD_STATUS}")
            endif()
            # Make executable on Unix
            if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
                file(CHMOD "${_TC_SOKOL_SHDC}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
            endif()
            message(STATUS "[${PROJECT_NAME}] sokol-shdc downloaded successfully")
        endif()

        # Output languages: Metal (macOS/iOS), HLSL (Windows), GLSL (Linux), WGSL (Web/WebGPU)
        set(_TC_SOKOL_SLANG "metal_macos:metal_ios:hlsl5:glsl300es:wgsl")

        set(_TC_SHADER_OUTPUTS "")
        foreach(_shader_src ${_TC_SHADER_SOURCES})
            set(_shader_out "${_shader_src}.h")
            list(APPEND _TC_SHADER_OUTPUTS ${_shader_out})

            get_filename_component(_shader_name ${_shader_src} NAME)
            add_custom_command(
                OUTPUT ${_shader_out}
                COMMAND ${_TC_SOKOL_SHDC} -i ${_shader_src} -o ${_shader_out} -l ${_TC_SOKOL_SLANG} --ifdef
                DEPENDS ${_shader_src}
                COMMENT "Compiling shader: ${_shader_name}"
            )
        endforeach()

        add_custom_target(${PROJECT_NAME}_shaders DEPENDS ${_TC_SHADER_OUTPUTS})
        add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}_shaders)
        message(STATUS "[${PROJECT_NAME}] Shader compilation enabled for ${_TC_SHADER_SOURCES}")
    endif()

    # Output settings
    if(ANDROID)
        # Android: build .so, then package into APK via post-build script
        set(_TC_APK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/bin/android")
        set(_TC_APK_STAGING "${CMAKE_CURRENT_BINARY_DIR}/apk_staging")
        set(_TC_LIB_DIR "${_TC_APK_STAGING}/lib/arm64-v8a")

        # Generate AndroidManifest.xml from template
        # Android package name segments must start with a letter
        string(REGEX REPLACE "^([^a-zA-Z])" "app\\1" _TC_SAFE_NAME "${PROJECT_NAME}")
        set(TC_APP_PACKAGE "com.trussc.${_TC_SAFE_NAME}")
        set(TC_APP_LIB_NAME "${PROJECT_NAME}")
        set(_TC_MANIFEST_TEMPLATE "${TRUSSC_DIR}/resources/android/AndroidManifest.xml.in")
        set(_TC_MANIFEST_OUT "${CMAKE_CURRENT_BINARY_DIR}/AndroidManifest.xml")
        configure_file("${_TC_MANIFEST_TEMPLATE}" "${_TC_MANIFEST_OUT}" @ONLY)

        # Output .so directly into staging
        file(MAKE_DIRECTORY "${_TC_LIB_DIR}")
        set_target_properties(${PROJECT_NAME} PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY "${_TC_LIB_DIR}"
            LIBRARY_OUTPUT_DIRECTORY_DEBUG "${_TC_LIB_DIR}"
            LIBRARY_OUTPUT_DIRECTORY_RELEASE "${_TC_LIB_DIR}"
            LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${_TC_LIB_DIR}"
        )

        # Post-build: package APK (if Android SDK build-tools available)
        find_program(_TC_AAPT aapt HINTS "$ENV{ANDROID_HOME}/build-tools/35.0.0" "$ENV{ANDROID_HOME}/build-tools/34.0.0")
        find_program(_TC_ZIPALIGN zipalign HINTS "$ENV{ANDROID_HOME}/build-tools/35.0.0" "$ENV{ANDROID_HOME}/build-tools/34.0.0")
        find_program(_TC_APKSIGNER apksigner HINTS "$ENV{ANDROID_HOME}/build-tools/35.0.0" "$ENV{ANDROID_HOME}/build-tools/34.0.0")

        if(_TC_AAPT AND _TC_ZIPALIGN AND _TC_APKSIGNER AND EXISTS "$ENV{HOME}/.android/debug.keystore")
            # Find android.jar
            set(_TC_ANDROID_JAR "$ENV{ANDROID_HOME}/platforms/android-35/android.jar")
            if(NOT EXISTS "${_TC_ANDROID_JAR}")
                set(_TC_ANDROID_JAR "$ENV{ANDROID_HOME}/platforms/android-34/android.jar")
            endif()

            file(MAKE_DIRECTORY "${_TC_APK_DIR}")
            set(_TC_UNSIGNED "${CMAKE_CURRENT_BINARY_DIR}/unsigned.apk")
            set(_TC_ALIGNED "${CMAKE_CURRENT_BINARY_DIR}/aligned.apk")
            set(_TC_APK_OUT "${_TC_APK_DIR}/${PROJECT_NAME}.apk")

            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${_TC_AAPT} package -f
                    -M "${_TC_MANIFEST_OUT}"
                    -I "${_TC_ANDROID_JAR}"
                    -F "${_TC_UNSIGNED}"
                    "${_TC_APK_STAGING}"
                COMMAND ${_TC_ZIPALIGN} -f 4 "${_TC_UNSIGNED}" "${_TC_ALIGNED}"
                COMMAND ${_TC_APKSIGNER} sign
                    --ks "$ENV{HOME}/.android/debug.keystore"
                    --ks-pass pass:android --key-pass pass:android
                    --out "${_TC_APK_OUT}" "${_TC_ALIGNED}"
                COMMENT "[${PROJECT_NAME}] Packaging APK → ${_TC_APK_OUT}"
            )
            message(STATUS "[${PROJECT_NAME}] APK auto-packaging enabled")
        else()
            message(STATUS "[${PROJECT_NAME}] Android SDK build-tools not found — APK packaging disabled (build .so only)")
        endif()

    elseif(EMSCRIPTEN)
        # Emscripten: HTML output
        set_target_properties(${PROJECT_NAME} PROPERTIES
            SUFFIX ".html"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
        )
        # Custom shell HTML path
        set(_TC_SHELL_FILE "${TC_ROOT}/core/platform/web/shell.html")
        # Common Emscripten link options
        target_link_options(${PROJECT_NAME} PRIVATE
            -sALLOW_MEMORY_GROWTH=1
            -sALLOW_TABLE_GROWTH=1
            -sFETCH=1
            -sASYNCIFY=1
            --shell-file=${_TC_SHELL_FILE}
        )
        # Backend-specific link options
        # TC_WEB_BACKEND is set in core/CMakeLists.txt (defaults to WGPU)
        if(NOT DEFINED TC_WEB_BACKEND)
            set(TC_WEB_BACKEND "WGPU")
        endif()
        if(TC_WEB_BACKEND STREQUAL "WGPU")
            target_link_options(${PROJECT_NAME} PRIVATE --use-port=emdawnwebgpu)
        else()
            target_link_options(${PROJECT_NAME} PRIVATE
                -sMIN_WEBGL_VERSION=2
                -sMAX_WEBGL_VERSION=2
            )
        endif()
        # Auto-preload bin/data folder if it exists and is non-empty
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bin/data")
            file(GLOB _TC_DATA_FILES "${CMAKE_CURRENT_SOURCE_DIR}/bin/data/*")
            if(_TC_DATA_FILES)
                target_link_options(${PROJECT_NAME} PRIVATE
                    --preload-file ${CMAKE_CURRENT_SOURCE_DIR}/bin/data@/data
                )
                message(STATUS "[${PROJECT_NAME}] Preloading data folder for Emscripten")
            endif()
        endif()
    elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        set_target_properties(${PROJECT_NAME} PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_BUNDLE_NAME "${MACOSX_BUNDLE_DISPLAY_NAME}"
            MACOSX_BUNDLE_GUI_IDENTIFIER "com.trussc.${PROJECT_NAME}"
            MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
            MACOSX_BUNDLE_INFO_PLIST "${TRUSSC_DIR}/resources/Info-iOS.plist.in"
            XCODE_GENERATE_SCHEME TRUE
            XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
            XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.trussc.${PROJECT_NAME}"
        )
        # Code signing: always allow automatic signing, let user pick team in Xcode
        set_target_properties(${PROJECT_NAME} PROPERTIES
            XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Apple Development"
            XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
            XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "YES"
            XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "YES"
        )
        if(TRUSSC_IOS_TEAM_ID)
            set_target_properties(${PROJECT_NAME} PROPERTIES
                XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${TRUSSC_IOS_TEAM_ID}"
            )
        endif()
        # Copy data folder into app bundle
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bin/data")
            file(GLOB_RECURSE _TC_DATA_FILES "${CMAKE_CURRENT_SOURCE_DIR}/bin/data/*")
            foreach(_data_file ${_TC_DATA_FILES})
                file(RELATIVE_PATH _rel_path "${CMAKE_CURRENT_SOURCE_DIR}/bin/data" "${_data_file}")
                get_filename_component(_rel_dir "${_rel_path}" DIRECTORY)
                set_source_files_properties("${_data_file}" PROPERTIES
                    MACOSX_PACKAGE_LOCATION "data/${_rel_dir}")
                target_sources(${PROJECT_NAME} PRIVATE "${_data_file}")
            endforeach()
        endif()
        trussc_setup_icon(${PROJECT_NAME})
    elseif(APPLE)
        set_target_properties(${PROJECT_NAME} PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_BUNDLE_NAME "${MACOSX_BUNDLE_DISPLAY_NAME}"
            MACOSX_BUNDLE_GUI_IDENTIFIER "com.trussc.${PROJECT_NAME}"
            MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
            MACOSX_BUNDLE_INFO_PLIST "${TRUSSC_DIR}/resources/Info.plist.in"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
            RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/bin"
            RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin"
            RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_SOURCE_DIR}/bin"
            RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_CURRENT_SOURCE_DIR}/bin"
            # Xcode: Generate scheme and set as default target
            XCODE_GENERATE_SCHEME TRUE
            XCODE_SCHEME_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        )
        trussc_setup_icon(${PROJECT_NAME})
    else()
        set_target_properties(${PROJECT_NAME} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
            RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/bin"
            RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin"
            RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_SOURCE_DIR}/bin"
            RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_CURRENT_SOURCE_DIR}/bin"
        )
        # Visual Studio: Set as startup project
        set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})
        # Windows: Setup icon
        trussc_setup_icon(${PROJECT_NAME})
    endif()

    message(STATUS "[${PROJECT_NAME}] TrussC app configured")
endmacro()
