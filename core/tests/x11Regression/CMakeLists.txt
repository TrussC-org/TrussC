# Production X11/EGL boundary regression plus shared GLX visibility coverage.
cmake_minimum_required(VERSION 3.16)
project(x11Regression CXX)

set(TC_X11_AVAILABLE FALSE)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_LIBRARIES X11 Xcursor Xi EGL GLESv2)
    check_cxx_source_compiles("
        #include <X11/Xlib.h>
        #include <X11/Xcursor/Xcursor.h>
        #include <X11/extensions/XInput2.h>
        #include <EGL/egl.h>
        #include <GLES3/gl3.h>
        int main() { XOpenDisplay(0); XcursorLibraryLoadCursor(0, 0);
            int a, b; XIQueryVersion(0, &a, &b);
            eglGetCurrentContext(); glGetString(GL_VERSION); }
    " TC_X11_EGL_DEPS)
    unset(CMAKE_REQUIRED_LIBRARIES)
    if(TC_X11_EGL_DEPS)
        set(TC_X11_AVAILABLE TRUE)
    endif()
    set(CMAKE_REQUIRED_LIBRARIES X11 GL)
    check_cxx_source_compiles("
        #include <GL/glx.h>
        int main() { glXGetCurrentContext(); }
    " TC_X11_GL_DEPS)
    unset(CMAKE_REQUIRED_LIBRARIES)
endif()

if(TC_X11_AVAILABLE)
    # The continuous visibility histories must also exercise the GLX dispatcher.
    include(CTest)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME x11Pacing COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../windowTick/x11-pacing-test")
    if(TC_X11_GL_DEPS)
        add_executable(x11VisibilityGLX ../windowTick/x11-regression.cpp)
        target_compile_features(x11VisibilityGLX PRIVATE cxx_std_20)
        target_compile_definitions(x11VisibilityGLX PRIVATE SOKOL_GLCORE)
        target_include_directories(x11VisibilityGLX PRIVATE ../../include/sokol)
        target_link_libraries(x11VisibilityGLX PRIVATE X11 Xcursor Xi GL)
        target_link_options(x11VisibilityGLX PRIVATE -Wl,--wrap=XGetWindowProperty)
        add_test(NAME x11VisibilityGLX COMMAND x11VisibilityGLX)
    else()
        message(STATUS "SKIP: x11VisibilityGLX requires GLX development libraries")
    endif()
    add_test(NAME x11Regression COMMAND x11Regression)
    add_executable(x11Regression ../windowTick/x11-regression.cpp)
    target_compile_features(x11Regression PRIVATE cxx_std_20)
    target_compile_definitions(x11Regression PRIVATE SOKOL_GLES3)
    target_include_directories(x11Regression PRIVATE ../../include/sokol)
    target_link_libraries(x11Regression PRIVATE X11 Xcursor Xi EGL GLESv2)
    foreach(boundary clock_gettime XPending XFlush XNextEvent XGetWindowAttributes poll
            eglMakeCurrent eglGetCurrentContext eglGetCurrentSurface eglGetCurrentDisplay eglSwapBuffers
            XGrabPointer XUngrabPointer XWarpPointer XSetSelectionOwner XGetSelectionOwner XInternAtom
            XConvertSelection XDeleteProperty XCheckTypedWindowEvent XGetWindowProperty
            XLookupString XFilterEvent XSendEvent XGetAtomName XFree)
        target_link_options(x11Regression PRIVATE "-Wl,--wrap=${boundary}")
    endforeach()
else()
    set(skip_message "SKIP: x11Regression requires Linux and X11/Xcursor/Xi/EGL/GLESv2 development libraries")
    message(STATUS "${skip_message}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/skip.cpp"
        "#include <cstdio>\nint main() { std::puts(\"${skip_message}\"); }\n")
    add_executable(x11Regression "${CMAKE_CURRENT_BINARY_DIR}/skip.cpp")
endif()
