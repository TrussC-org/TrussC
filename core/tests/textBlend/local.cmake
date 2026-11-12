# allCoreTests includes each selected test's local build steps.
if(EMSCRIPTEN)
    target_link_options(${PROJECT_NAME} PRIVATE
        "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/../renderTargetState/src/fixture.ttf@/data/textBlend-fixture.ttf")
endif()
