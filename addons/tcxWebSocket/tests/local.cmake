# Instrument the ingress parser and its regression harness without rebuilding
# unrelated core dependencies with sanitizer flags.
option(TC_WEBSOCKET_SANITIZE "Enable ingress test sanitizers" OFF)
if(TC_WEBSOCKET_SANITIZE AND NOT MSVC)
    foreach(target ${PROJECT_NAME} tcxWebSocket)
        target_compile_options(${target} PRIVATE -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
    endforeach()
    target_link_options(${PROJECT_NAME} PRIVATE -fsanitize=address,undefined)
endif()
