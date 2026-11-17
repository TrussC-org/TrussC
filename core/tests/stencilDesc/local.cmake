if(EMSCRIPTEN)
    target_link_options(${PROJECT_NAME} PRIVATE -sEXIT_RUNTIME=0
        "--pre-js=${CMAKE_CURRENT_LIST_DIR}/../common/web-args.js")
    set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY
        LINK_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../common/web-args.js")
endif()
# Use the app's data folder so Web preload and iOS bundling include the font.
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/../renderTargetState/src/fixture.ttf"
    "${CMAKE_CURRENT_SOURCE_DIR}/bin/data/stencilDesc-fixture.ttf" COPYONLY)
