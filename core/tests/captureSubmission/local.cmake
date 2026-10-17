# Enable trace implementation only in this test build.
target_compile_definitions(TrussC PRIVATE SOKOL_TC_TEST_HOOKS)
