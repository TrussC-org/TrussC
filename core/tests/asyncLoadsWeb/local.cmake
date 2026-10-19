if(EMSCRIPTEN)
    target_link_options(${PROJECT_NAME} PRIVATE
        "-Wl,--wrap=emscripten_fetch"
        "-Wl,--wrap=emscripten_fetch_close"
        "-Wl,--wrap=emscripten_async_call"
        "-Wl,--wrap=emscripten_get_now")
endif()
