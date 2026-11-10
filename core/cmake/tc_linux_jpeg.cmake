# Linux uses the system libjpeg-turbo through the classic libjpeg API.
find_package(PkgConfig REQUIRED)
pkg_check_modules(TC_JPEG QUIET IMPORTED_TARGET libjpeg)
if(NOT TC_JPEG_FOUND)
    message(FATAL_ERROR "Linux JPEG decoding requires libjpeg-turbo. Install libjpeg-dev (Ubuntu: libjpeg-turbo8-dev; Debian/Pi OS: libjpeg62-turbo-dev). pkg-config libjpeg must succeed.")
endif()
include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_LIBRARIES PkgConfig::TC_JPEG)
check_cxx_source_compiles("
    #include <cstddef>
    #include <cstdio>
    #include <jpeglib.h>
    int main() { J_COLOR_SPACE rgba = JCS_EXT_RGBA; return int(rgba); }
" TC_JPEG_HAS_EXT_RGBA)
unset(CMAKE_REQUIRED_LIBRARIES)
if(NOT TC_JPEG_HAS_EXT_RGBA)
    message(FATAL_ERROR "Linux requires libjpeg-turbo with JCS_EXT_RGBA; this libjpeg is unsupported. Install libjpeg-dev from your distribution (libjpeg-turbo8-dev / libjpeg62-turbo-dev).")
endif()
