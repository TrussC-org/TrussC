# Synthetic inputs exercise draw paths without codec or addon dependencies.
trussc_compile_shaders(targetStateLut "${TRUSSC_DIR}/../addons/tcxLut/src/shaders")
trussc_compile_shaders(targetStateHap "${TRUSSC_DIR}/../addons/tcxHap/src/shaders")
target_link_libraries(${PROJECT_NAME} PRIVATE targetStateLut_shaders targetStateHap_shaders)
