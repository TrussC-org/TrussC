# Synthetic LUT/Hap draw paths are shared by multiple core tests. Declare
# their source-tree outputs once when those tests are combined into one app.
include_guard(DIRECTORY)
trussc_compile_shaders(coreTestLut "${TRUSSC_DIR}/../addons/tcxLut/src/shaders")
trussc_compile_shaders(coreTestHap "${TRUSSC_DIR}/../addons/tcxHap/src/shaders")
target_link_libraries(${PROJECT_NAME} PRIVATE coreTestLut_shaders coreTestHap_shaders)
