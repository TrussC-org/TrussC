if(EMSCRIPTEN)
    target_link_options(${PROJECT_NAME} PRIVATE
        # Font URLs use the bounded streaming backend. Mock its JS transport,
        # retaining the real C++ completion and close/lifetime handling.
        "-Wl,--wrap=startFontStream"
        "-Wl,--wrap=cancelFontStream"
        "-Wl,--wrap=emscripten_async_call"
        "-Wl,--wrap=emscripten_get_now")
endif()
