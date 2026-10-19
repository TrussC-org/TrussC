# The contract injects a throwing writer to verify temporary cleanup. Production
# Web defaults disable catching; enable it for this test executable only.
if(EMSCRIPTEN)
    target_compile_options(${PROJECT_NAME} PRIVATE -fexceptions)
    target_link_options(${PROJECT_NAME} PRIVATE -fexceptions)
endif()
