if(EMSCRIPTEN)
    target_link_options(${PROJECT_NAME} PRIVATE
        "-Wl,--wrap=emscripten_async_call"
        "-Wl,--wrap=emscripten_get_now"
        "-Wl,--wrap=emscripten_set_timeout"
        "-Wl,--wrap=emscripten_clear_timeout")
endif()
