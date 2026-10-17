trussc_compile_shaders(pipelineLut "${TRUSSC_DIR}/../addons/tcxLut/src/shaders")
trussc_compile_shaders(pipelineHap "${TRUSSC_DIR}/../addons/tcxHap/src/shaders")
target_link_libraries(${PROJECT_NAME} PRIVATE pipelineLut_shaders pipelineHap_shaders)
